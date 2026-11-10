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

#ifndef PDFAPPTOOLGUI_GEOMETRY_H
#define PDFAPPTOOLGUI_GEOMETRY_H

#include "pdfappglobal.h"
#include "pdfappdeclarativetoolgui.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfapptoolboxesoverlay.h"
#include "pdfapptoolcropoverlay.h"
#include "pdfapptoolpagestage.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolpanelparts.h"
#include "pdfdocumenttoolshell.h"

#include "pdfanchorgrid.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdfdocument.h"
#include "pdffluentwidgets.h"
#include "pdfpagegeometry.h"

#include <QPointer>
#include <QToolButton>
#include <QWidget>

#include <map>
#include <vector>

class QAbstractButton;
class QGridLayout;
class QModelIndex;

namespace pdf
{

class PDFAnchorGrid;
class PDFAppCropOverlay;
class PDFAppSettingControl;
class PDFAppThumbnailListView;
class PDFAppThumbnailsModel;
class PDFAppThumbnailScheduler;
class PDFAppToolPageCanvas;
class PDFAppToolPageSelectionBinder;
class PDFAppToolParameterModel;
class PDFDoubleSpinBox;
class PDFKeyValueList;
class PDFLabel;
class PDFPageSelection;
class PDFThumbnailDelegate;
class PDFTileRow;

/// The tools pages.crop, pages.resize and pages.boxes (package B of step 3a of phase 2, plan
/// 9.2); its file defines the elements of the panel builder of package B (addAnchorMenu,
/// addAnchorField, addMarginFields)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIGeometry
{
public:
    PDFAppToolGUIGeometry() = delete;

    /// Registers what is not registered yet: the tools in PDFDocumentToolRegistry and their
    /// GUI in PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerGeometryTools();
};

/// Rows of parameters in a card (Margins 389:22382, Crop box 389:23500, Margins and Content
/// 391:23146, New box = trim box + 391:26688): a Real parameter in millimeters is a row with its
/// title and a spin box with " mm" (PDFAppSettingControl through the model, one undo step after
/// the rest of the spin box), a Bool parameter is a check box with its title; the note below the
/// rows (Regular 12 text/tertiary). The rows are never mirrored for right-to-left in their
/// order: Left, Top, Right, Bottom belong to the page.
///
/// Ownership: built by PDFAppToolPanelBuilder::addMarginFields into the panel of the tool; the
/// binders are its children, the widgets are held by QPointer. No signals.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolMarginFields : public PDFAppToolCard
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolCard;

public:
    /// Width of a field (389:22382: 140)
    static constexpr int FIELD_WIDTH = 140;

    explicit PDFAppToolMarginFields(PDFDocumentToolGUI* gui, const QStringList& parameters, const QString& note, QWidget* parent);
    virtual ~PDFAppToolMarginFields() override;

    /// The spin box of a Real parameter, the check box of a Bool one (nullptr = none)
    PDFDoubleSpinBox* getSpinBox(const QString& parameter) const;
    QAbstractButton* getCheckBox(const QString& parameter) const;
    PDFLabel* getNoteLabel() const { return m_note; }

    /// The row of the parameter is enabled
    void setRowEnabled(const QString& parameter, bool isEnabled);

    /// The row of the parameter (or of a widget row) is shown (Width and Height of Custom)
    void setRowVisible(const QString& key, bool isVisible);

    /// A row of another editor of the width of a field above the note (the anchor combo of
    /// Images to PDF 336:1976); the card takes the editor
    void addWidgetRow(const QString& key, const QString& label, QWidget* editor);

    /// The editor of a row (a spin box, a combo box, a check box, a widget row), nullptr = none
    QWidget* getEditor(const QString& key) const;

private:
    struct Row
    {
        QPointer<PDFAppSettingControl> control;
        QPointer<QWidget> label;
        QPointer<QWidget> editor;
    };

    std::map<QString, Row> m_rows;
    QPointer<PDFLabel> m_note;
    QPointer<QGridLayout> m_grid;
    int m_rowCount = 0;
};

/// The panel of an anchor (391:23821: the caption Anchor, PDFAnchorGrid, the name of the chosen
/// anchor and its description), the toolbar menu of Resize and the popover of the field of Add
/// Image as Page and Images to PDF. A choice closes the panel and writes the parameter (one undo
/// step); the grid follows the model (Undo, Redo).
///
/// Ownership: the panel belongs to its button (the toolbar or the field), it holds the model by
/// QPointer. The grid is its child.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolAnchorPanel : public PDFAppToolPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolPanelPopup;

public:
    explicit PDFAppToolAnchorPanel(PDFAppDocumentView* view, PDFAppToolParameterModel* model, const QString& parameter, int cellSize, int panelWidth, QWidget* parent);
    virtual ~PDFAppToolAnchorPanel() override;

    PDFAnchorGrid* getGrid() const { return m_grid; }
    PDFLabel* getNameLabel() const { return m_name; }

    /// The button, which says the chosen anchor by its format ("Anchor: %1"; empty = the title)
    void setTitleButton(QToolButton* button, const QString& titleFormat);

    /// The title of the chosen option of the parameter ("Middle center")
    QString getAnchorTitle() const;

private:
    void refresh();
    void onAnchorChosen(Qt::Alignment anchor);
    void onParameterChanged(const QString& name);

    QPointer<PDFAppToolParameterModel> m_model;
    QString m_parameter;
    QPointer<PDFAnchorGrid> m_grid;
    QPointer<PDFLabel> m_name;
    QPointer<QToolButton> m_titleButton;
    QString m_titleFormat;
};

/// The combo box of an anchor (Images to PDF 356:9556, Add Image as Page 388:18837, K14): a combo
/// box of the nine anchors ("Middle center"), whose list is the popover PDFAppToolAnchorPanel with
/// the cells of 36 x 36 below it. The arrows on the closed combo box choose another anchor as in
/// any combo box; the panel and the arrows write the parameter (one undo step), the combo box
/// follows the model (Undo, Redo).
///
/// Ownership: the panel is its child; it holds the model by a QPointer. No signals of its own.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolAnchorComboBox : public PDFComboBox
{
    Q_OBJECT

private:
    using BaseClass = PDFComboBox;

public:
    explicit PDFAppToolAnchorComboBox(PDFDocumentToolGUI* gui, const QString& parameter, QWidget* parent);
    virtual ~PDFAppToolAnchorComboBox() override;

    PDFAppToolAnchorPanel* getPanel() const { return m_panel; }

    /// Opens the panel below the combo box instead of the list
    virtual void showPopup() override;
    virtual void hidePopup() override;

private:
    void refresh();
    void onActivated(int index);
    void onParameterChanged(const QString& name);

    QPointer<PDFAppToolParameterModel> m_model;
    QString m_parameter;
    QPointer<PDFAppToolAnchorPanel> m_panel;
};

/// The field Anchor of a panel (Add Image as Page 388:18837, Images to PDF 356:9556): the label
/// (Regular 13 text/secondary) and the combo box PDFAppToolAnchorComboBox of 140 at the end, as
/// the other fields of the card (field-Anchor 336:2317).
///
/// Ownership: built by PDFAppToolPanelBuilder::addAnchorField; the combo box is its child. No signals.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolAnchorField : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppToolAnchorField(PDFDocumentToolGUI* gui, const QString& parameter, const QString& label, QWidget* parent);
    virtual ~PDFAppToolAnchorField() override;

    PDFAppToolAnchorComboBox* getComboBox() const { return m_comboBox; }
    PDFAppToolAnchorPanel* getPanel() const { return m_comboBox ? m_comboBox->getPanel() : nullptr; }

    /// Opens the panel below the combo box
    void openPanel();

private:
    QPointer<PDFAppToolAnchorComboBox> m_comboBox;
};

/// The common part of Crop Pages, Resize Pages and Set Page Boxes (389:22382, 391:23146,
/// 391:26688): the left panel has the page selection of the pages the tool changes and the
/// thumbnails of the document; the thumbnail clicked is the page of the preview (K48, the
/// current page of the view at the entry). The canvas is PDFAppToolPageCanvas with the page of
/// the preview; the tool lays its overlays over it and says its texts.
///
/// Ownership: the shell owns this GUI and the parts; the canvas is the canvas page (the view
/// takes it during the mode), the thumbnails and the page selection belong to the left panel.
/// Every part is held by a QPointer. The thumbnails get the document as a shared value; the
/// stages copy their own values for their workers.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageGeometryToolGUI : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    explicit PDFAppPageGeometryToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppPageGeometryToolGUI() override;

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onDocumentChanged() override;
    virtual bool isConfirmEnabled() const override;
    virtual QVariantMap getRunParameters() const override;

    /// The page of the preview (index from 0) and its change (a click on its thumbnail); the view
    /// of the document goes to it, so the page item of the status bar says it (K48)
    PDFInteger getPreviewPage() const { return m_previewPage; }
    void setPreviewPage(PDFInteger page);

    /// The values of the model with the panel-only ones of a run (the reference page of Crop)
    QVariantMap getValues() const;

    /// The pages of the selection (indices from 0) - as the core decides them
    std::vector<PDFInteger> getTargetPages() const;

    /// The document shown in the tab and its page (nullptr = none)
    PDFDocumentPointer getShownDocument() const;
    const PDFPage* getPage(PDFInteger page) const;
    PDFInteger getPageCount() const;

    /// The error of the values, which the panels say (empty = none): the validation of the core
    QString getValuesError() const { return m_valuesError; }

    /// Parts (nullptr = none)
    PDFAppToolPageCanvas* getCanvas() const { return m_canvas; }
    PDFPageSelection* getPageSelection() const { return m_pageSelection; }
    PDFAppThumbnailListView* getThumbnailView() const { return m_thumbnailView; }

protected:
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;

    /// Number of the stages of the canvas (Resize: Now and After)
    virtual int getStageCount() const { return 1; }

    /// The label of the page selection ("Pages to crop")
    virtual QString getSelectionLabel() const = 0;

    /// The tool shows its values (the panels, the canvas, the overlays); called after every
    /// change of the values, of the page of the preview and of the document
    virtual void updateTool() = 0;

    /// The stages of the canvas have a new page (the overlays are made here)
    virtual void onCanvasCreated() = 0;

    /// The shell has another state (Deciding: nothing can be changed, 389:24633)
    virtual void onShellStateChanged(pdf::PDFDocumentToolShell::State state);

    /// Adds the values, which only the GUI knows, to the values of the model (default: nothing)
    virtual void completeValues(QVariantMap* values) const;

    /// "Page 8" of the preview page
    QString getPreviewPageTitle() const;

    /// Below the result dialog: nothing of the tool can be changed
    bool isDeciding() const { return m_isDeciding; }

    /// Everything after a change: the validation of the core, the tool, the Primary button, the
    /// status bar (the last calls: they may delete this GUI)
    void updateAll();

private:
    void updateThumbnails();
    void onThumbnailClicked(const QModelIndex& index);
    void onShellStateChangedSlot(pdf::PDFDocumentToolShell::State state);
    void followPages();

    /// The view of the document shows the page of the preview (a foreign call: the last one)
    void showPreviewPageInView();

    QPointer<PDFAppToolPageCanvas> m_canvas;
    QPointer<PDFPageSelection> m_pageSelection;
    QPointer<PDFAppToolPageSelectionBinder> m_binder;
    QPointer<PDFAppThumbnailListView> m_thumbnailView;
    QPointer<PDFAppThumbnailsModel> m_thumbnailModel;
    QPointer<PDFThumbnailDelegate> m_thumbnailDelegate;
    QPointer<PDFAppThumbnailScheduler> m_thumbnailScheduler;
    PDFDocumentPointer m_pageDocument;  ///< The version, whose pages the values name
    PDFInteger m_previewPage = -1;
    QString m_valuesError;
    bool m_isDeciding = false;
    bool m_isShellConnected = false;
};

/// Crop Pages (pages.crop, 389:22382 ... 405:32319): Crop: Equal margins / Custom box, the toggle
/// Remove Crop; the box over the page of the preview (PDFAppCropOverlay) - a drag of the box of
/// the equal margins makes it a custom box; Margins or Crop box, Result, the callouts.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCropToolGUI : public PDFAppPageGeometryToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPageGeometryToolGUI;

public:
    explicit PDFAppCropToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppCropToolGUI() override;

    virtual QString getConfirmText() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;

    /// The mode of the values and the mode of the menu (the last of the equal margins and the
    /// custom box, also while Remove Crop is on)
    QString getMode() const;
    QString getMenuMode() const;

    /// The toolbar: a mode of the menu, the toggle Remove Crop
    void chooseMode(const QString& mode);
    void toggleRemoveCrop();

    /// The new crop box of the page of the preview in its coordinates (empty = invalid)
    QRectF getPreviewCropRect() const;

    /// Parts (nullptr = none)
    QToolButton* getModeButton() const;
    PDFTileRow* getModeRow(const QString& mode) const;
    QToolButton* getRemoveButton() const { return m_removeButton; }
    PDFAppCropOverlay* getOverlay() const { return m_overlay; }
    PDFAppToolMarginFields* getMarginFields() const { return m_marginFields; }
    PDFAppToolMarginFields* getBoxFields() const { return m_boxFields; }
    PDFKeyValueList* getResult() const { return m_result; }
    PDFAppToolCallout* getErrorCallout() const { return m_errorCallout; }
    PDFAppToolCallout* getSizesCallout() const { return m_sizesCallout; }
    PDFAppToolCallout* getHiddenCallout() const { return m_hiddenCallout; }
    PDFAppToolCallout* getRemoveCallout() const { return m_removeCallout; }

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual QString getSelectionLabel() const override;
    virtual void updateTool() override;
    virtual void onCanvasCreated() override;
    virtual void completeValues(QVariantMap* values) const override;

private:
    void onModeRowClicked();
    void onOverlayRectChanged(const QRectF& rect);
    void onOverlayRectChosen(const QRectF& rect);

    /// The sizes of the target pages ("46 pages are A4, 2 pages are A3"), empty for one size
    QString getSizesText(int* sizeCount) const;

    QPointer<PDFAppToolPanelPopup> m_modePanel;
    std::map<QString, QPointer<PDFTileRow>> m_modeRows;
    QPointer<QToolButton> m_removeButton;
    QPointer<PDFAppCropOverlay> m_overlay;
    QPointer<PDFAppToolSectionHeader> m_marginsHeader;
    QPointer<PDFAppToolMarginFields> m_marginFields;
    QPointer<PDFAppToolSectionHeader> m_boxHeader;
    QPointer<PDFAppToolMarginFields> m_boxFields;
    QPointer<PDFKeyValueList> m_result;
    QPointer<PDFAppToolCallout> m_errorCallout;
    QPointer<PDFAppToolCallout> m_sizesCallout;
    QPointer<PDFAppToolCallout> m_hiddenCallout;
    QPointer<PDFAppToolCallout> m_removeCallout;
    QString m_lastMode;     ///< The mode of the menu while Remove Crop is on
};

/// Resize Pages (pages.resize, 391:23146 ... 391:25948): Size, Orientation, Content, Anchor in the
/// toolbar; the page of the preview Now and After (the sheet with the content placed and the area
/// inside of the margins dashed, red when the margins leave no room); Page size (Custom),
/// Margins, Content, Summary.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppResizeToolGUI : public PDFAppPageGeometryToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPageGeometryToolGUI;

public:
    explicit PDFAppResizeToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppResizeToolGUI() override;

    virtual QString getConfirmText() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual std::vector<PDFSecurityHandler::Permission> getRunPermissions() const override;

    /// The placement of the page of the preview
    PDFPageGeometry::ResizePlacement getPreviewPlacement() const;

    /// Parts (nullptr = none)
    PDFAnchorGrid* getAnchorGrid() const { return m_anchorGrid; }
    PDFAppPageBoxesOverlay* getMarginsOverlay() const { return m_marginsOverlay; }
    PDFAppToolMarginFields* getSizeFields() const { return m_sizeFields; }
    PDFAppToolMarginFields* getMarginFields() const { return m_marginFields; }
    PDFAppToolMarginFields* getContentFields() const { return m_contentFields; }
    PDFKeyValueList* getSummary() const { return m_summary; }
    PDFAppToolCallout* getErrorCallout() const { return m_errorCallout; }
    QWidget* getKeepSizeNote() const { return m_keepSizeNote; }

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual int getStageCount() const override { return 2; }
    virtual QString getSelectionLabel() const override;
    virtual void updateTool() override;
    virtual void onCanvasCreated() override;

private:
    /// "Letter", "A4" or "216 × 279 mm" of a size as shown in millimeters
    static QString getSizeName(const QSizeF& sizeMM);

    /// The values are Scale to fit / a custom size / the size of the page
    bool isScaleToFit() const;
    QString getPageSizeId() const;

    QPointer<QToolButton> m_orientationButton;
    QPointer<PDFAnchorGrid> m_anchorGrid;
    QPointer<PDFAppPageBoxesOverlay> m_marginsOverlay;
    QPointer<PDFAppToolSectionHeader> m_sizeHeader;
    QPointer<PDFAppToolMarginFields> m_sizeFields;
    QPointer<PDFAppToolMarginFields> m_marginFields;
    QPointer<PDFAppToolMarginFields> m_contentFields;
    QPointer<PDFKeyValueList> m_summary;
    QPointer<PDFAppToolCallout> m_errorCallout;
    QPointer<QWidget> m_keepSizeNote;
};

/// Set Page Boxes (pages.boxes, 391:26688 ... 391:28535): Based on, Write to (a multi-select list:
/// the rows are checked and the panel stays open, K19), the toggle of the boxes in the preview;
/// the five boxes over the whole sheet (the new box thick), their legend; New box = <box> +,
/// Boxes of page N, the callouts.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBoxesToolGUI : public PDFAppPageGeometryToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPageGeometryToolGUI;

public:
    explicit PDFAppBoxesToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppBoxesToolGUI() override;

    virtual QString getConfirmText() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;

    /// The boxes of Write to
    std::vector<PDFPageGeometry::Box> getWriteTo() const;

    /// A box of Write to is switched (one undo step; the panel stays open)
    void toggleWriteTo(PDFPageGeometry::Box box);

    /// The boxes are shown in the preview (the toggle of the toolbar)
    bool isBoxesShown() const { return m_isBoxesShown; }
    void setBoxesShown(bool isShown);

    /// Parts (nullptr = none)
    QToolButton* getWriteToButton() const;
    PDFTileRow* getWriteToRow(PDFPageGeometry::Box box) const;
    QToolButton* getShowBoxesButton() const { return m_showBoxesButton; }
    PDFAppPageBoxesOverlay* getOverlay() const { return m_overlay; }
    PDFAppPageBoxesLegend* getLegend() const { return m_legend; }
    PDFAppToolMarginFields* getMarginFields() const { return m_marginFields; }
    PDFKeyValueList* getBoxesList() const { return m_boxes; }
    PDFAppToolCallout* getErrorCallout() const { return m_errorCallout; }
    PDFAppToolCallout* getWarningCallout() const { return m_warningCallout; }

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual QString getSelectionLabel() const override;
    virtual void updateTool() override;
    virtual void onCanvasCreated() override;

private:
    void onWriteToRowClicked();
    void onShowBoxesClicked();

    /// The color of a box (391:26688)
    static PDFThemeColors::Color getBoxColor(PDFPageGeometry::Box box);

    QPointer<PDFAppToolPanelPopup> m_writeToPanel;
    std::map<PDFPageGeometry::Box, QPointer<PDFTileRow>> m_writeToRows;
    QPointer<QToolButton> m_showBoxesButton;
    QPointer<PDFAppPageBoxesOverlay> m_overlay;
    QPointer<PDFAppPageBoxesLegend> m_legend;
    QPointer<PDFAppToolSectionHeader> m_marginsHeader;
    QPointer<PDFAppToolMarginFields> m_marginFields;
    QPointer<PDFAppToolSectionHeader> m_boxesHeader;
    QPointer<PDFKeyValueList> m_boxes;
    QPointer<PDFAppToolCallout> m_errorCallout;
    QPointer<PDFAppToolCallout> m_warningCallout;
    bool m_isBoxesShown = true;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_GEOMETRY_H
