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

#ifndef PDFAPPTOOLGUI_INTERLEAVE_H
#define PDFAPPTOOLGUI_INTERLEAVE_H

#include "pdfappglobal.h"
#include "pdfappdeclarativetoolgui.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolmergemodel.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"

#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"

#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QWidget>

#include <array>

namespace pdf
{

/// The canvas of Interleave Pages (document-canvas of 385:13263): bg/track, the bar "Result · 12
/// pages" (Semi Bold 13), the tiles of the pages of the result - the pictures of the pages of the
/// two scans, the number with the dot of the hue of its scan (blue front, green back) and the
/// caption "front 1" / "back 6" (PDFAppToolMergePageModel with the plan of
/// PDFDocumentPagePlan::interleave; the side without a pair has the badge alert-triangle,
/// 385:14487) - and the hint below them. Without two scans the drop zone "Add the two scans"
/// (385:13738) is in the place of the tiles.
///
/// Ownership: the canvas owns the view, the model, the delegate and the scheduler; the GUI holds
/// the canvas by a QPointer (the view of the tool owns it during the mode). No signal of its own.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInterleaveCanvas : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Geometry of 385:13263 (the padding of the canvas, the bar, the gaps; the slot of the page of
    /// the tiles in the size Small 102 x 140, K8)
    static constexpr int PADDING = 20;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int BAR_GAP = 12;
    static constexpr int HINT_GAP = 10;
    static constexpr int SLOT_WIDTH = 102;
    static constexpr int SLOT_HEIGHT = 140;

    explicit PDFAppInterleaveCanvas(QWidget* parent);
    virtual ~PDFAppInterleaveCanvas() override;

    void setHeaderText(const QString& text);
    void setHintText(const QString& text);

    /// The tiles are shown (two usable scans), else the empty state
    void setEmpty(bool isEmpty);
    bool isEmpty() const;

    void setTileSize(PDFAppToolPageGrid::TileSize size);

    PDFLabel* getHeaderLabel() const { return m_header.data(); }
    PDFAppToolPageGridView* getView() const { return m_view.data(); }
    PDFAppToolMergePageModel* getModel() const { return m_model.data(); }
    PDFPageTileDelegate* getDelegate() const { return m_delegate.data(); }
    PDFAppParagraph* getHintLabel() const { return m_hint.data(); }
    QWidget* getEmptyState() const { return m_emptyState.data(); }
    QPushButton* getChooseFilesButton() const { return m_chooseFilesButton.data(); }

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QPointer<PDFLabel> m_header;
    QPointer<PDFAppToolPageGridView> m_view;
    QPointer<PDFAppToolMergePageModel> m_model;
    QPointer<PDFPageTileDelegate> m_delegate;
    QPointer<PDFAppThumbnailScheduler> m_scheduler;
    QPointer<PDFAppParagraph> m_hint;
    QPointer<QWidget> m_emptyState;
    QPointer<QPushButton> m_chooseFilesButton;
};

/// Interleave Pages (pages.interleave; Figma 385:13263 … 385:14973; plan of step 3 of phase 2,
/// 2.3, 5.7, K28, K29, K31, K37, K41, K47, Q8) in its own tab:
///  - the toolbar: Back sides (Reversed / In order), Extra pages (At the end / Leave out) - the
///    parameters of the core, PDFAppToolEnumMenu - and Thumbnails (the size of the tiles, K8);
///  - the panel Sources: the slots Front sides and Back sides (getInputRules: exactly two PDF
///    files, K41), each with its scan (the tile of its letter and hue, the pages, Change…) or,
///    empty, "Drop a PDF here" with Choose… (385:13738); Swap between them (K28: one undo step);
///  - the canvas: PDFAppInterleaveCanvas; the right panel: Summary (Front sides, Back sides,
///    Extra pages, Result), the amber callout of the sides without a pair (385:14487), "How
///    Interleave works" without the scans.
///
/// Values and Undo / Redo: the parameter "inputs" (the ids of the list: front, back) is one undo
/// step of Swap and Change…; Back sides and Extra pages are parameters of the core. The first
/// values follow the list without an undo step (K21). The run gets the usable inputs only.
///
/// Ownership (checklist point 1): the shell owns this GUI and the parts; the parts are held by
/// QPointer (they are made again after a change of the language). The list of the inputs
/// belongs to the tab (QPointer). The panel of Change… is a child of the left panel, held by a
/// QPointer, made again for each opening (deleteLater of the old one).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInterleaveToolGUI : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// The slots of the scans
    enum Slot
    {
        FrontSlot = 0,
        BackSlot = 1,
        SlotCount = 2
    };

    explicit PDFAppInterleaveToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppInterleaveToolGUI() override;

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual QString getOwnTabLossText() const override;
    virtual QString getOwnTabPageText() const override;
    virtual QString getOwnTabZoomText() const override;
    virtual PDFAppToolInputRules getInputRules() const override;
    virtual QVariantMap getRunParameters() const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void setContentEnabled(bool isEnabled) override;

    /// The ids of the scans in their slots (an empty id = an empty slot)
    std::array<QString, SlotCount> getSlotInputIds() const;

    /// Swap (K28): the front sides become the back sides and back (one undo step)
    void swapSlots();

    /// Puts the file (a PDF) into the slot instead of its scan (Choose…, Change…; one undo step);
    /// a refused file is said in the status bar
    void setSlotFile(Slot slot, const QString& filePath);

    /// Puts a snapshot of the open document into the slot (Change… › an open document)
    void setSlotDocument(Slot slot, const QString& documentId);

    /// Choose… / Change… › PDF file…: the dialog of the files, then setSlotFile
    void chooseSlotFile(Slot slot);

    /// The size of the tiles (Thumbnails: Small / Medium / Large)
    void setTileSize(PDFAppToolPageGrid::TileSize size);

    /// Texts of the tiles of the result ("front 1", "back 6") in their order
    QStringList getTileCaptions() const;

    /// Parts (tests)
    PDFAppInterleaveCanvas* getCanvas() const { return m_canvas.data(); }
    PDFAppToolPanelHeader* getSourcesHeader() const { return m_sourcesHeader.data(); }
    PDFLabel* getSlotCaption(Slot slot) const { return m_slotCaptions[slot].data(); }
    PDFAppToolInputRow* getSlotRow(Slot slot) const { return m_slotRows[slot].data(); }
    QWidget* getSlotEmpty(Slot slot) const { return m_slotEmpty[slot].data(); }
    QPushButton* getSwapButton() const { return m_swapButton.data(); }
    PDFKeyValueList* getSummary() const { return m_summary.data(); }
    PDFAppToolCallout* getUnevenCallout() const { return m_unevenCallout.data(); }
    QWidget* getHowSection() const { return m_howSection.data(); }
    PDFAppToolPanelPopup* getChangePanel() const { return m_changePanel.data(); }
    PDFAppToolTileSizeMenu* getTileSizeMenu() const { return m_tileSizeMenu.data(); }

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;

private:
    /// The usable scans of the slots (both or none: the plan needs both)
    bool hasBothScans() const;
    PDFInteger getSlotPageCount(Slot slot) const;

    void connectList();
    void initializeValues();
    void readValues();
    void setInputIds(const QStringList& ids);

    void onListChanged();
    void onListInputChanged(const QString& inputId);
    void onListDestroyed();
    void onSlotLinkActivated(const QString& inputId);
    void onChooseFrontClicked();
    void onChooseBackClicked();
    void onChangeRowClicked();
    void onTileSizeChosen(pdf::PDFAppToolPageGrid::TileSize size);
    void onCanvasChooseFiles();

    /// Opens the panel of Change… of the slot at the anchor
    void openChangePanel(Slot slot, QWidget* anchor);

    void updateAll();
    void updateSources();
    void updateCanvas();
    void updateSummary();

    /// The row of the scan of the slot
    PDFAppToolInputRow::Content createRowContent(Slot slot, const PDFAppToolInput& input) const;

    /// "result pages 1, 3, 5 …" of the slot
    QString getResultPagesText(Slot slot) const;

    /// "6 front and 6 back sides"
    QString getSidesText() const;

    QPointer<PDFAppToolInputList> m_list;
    QPointer<PDFAppInterleaveCanvas> m_canvas;
    QPointer<PDFAppToolPanelHeader> m_sourcesHeader;
    std::array<QPointer<PDFLabel>, SlotCount> m_slotCaptions;
    std::array<QPointer<PDFAppToolInputRow>, SlotCount> m_slotRows;
    std::array<QPointer<QWidget>, SlotCount> m_slotEmpty;
    QPointer<QPushButton> m_swapButton;
    QPointer<PDFKeyValueList> m_summary;
    QPointer<PDFAppToolCallout> m_unevenCallout;
    QPointer<QWidget> m_summarySection;
    QPointer<QWidget> m_howSection;
    QPointer<PDFAppToolPanelPopup> m_changePanel;
    QPointer<PDFAppToolTileSizeMenu> m_tileSizeMenu;
    QPointer<QWidget> m_leftPanel;
    Slot m_changeSlot = FrontSlot;
    PDFAppToolPageGrid::TileSize m_tileSize = PDFAppToolPageGrid::TileSize::Small;
    bool m_isTileSizeRead = false;
    bool m_isValuesInitialized = false;
    bool m_isChangingList = false;
    bool m_isRunning = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_INTERLEAVE_H
