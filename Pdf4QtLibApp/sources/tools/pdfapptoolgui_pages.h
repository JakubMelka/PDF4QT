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

#ifndef PDFAPPTOOLGUI_PAGES_H
#define PDFAPPTOOLGUI_PAGES_H

#include "pdfappglobal.h"
#include "pdfappdeclarativetoolgui.h"
#include "pdfapptoolpagegrid.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpainter.h"
#include "pdfdocument.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QToolButton>

#include <map>
#include <vector>

namespace pdf
{

class PDFAppToolPageSelectionBinder;

/// The tools pages.rotate and pages.remove (package 2A of the plan of the first batch, chapter 9)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIPages
{
public:
    PDFAppToolGUIPages() = delete;

    /// Registers what is not registered yet: the tools in PDFDocumentToolRegistry and
    /// their GUI in PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerTools();
};

/// Rows of the pending changes of a tool, which works on pages (Pending rotation 389:20403,
/// Marked for removal 388:20818), for PDFAppToolPendingList (PDFTileRowDelegate, the kind
/// Pending 46): a row of each page, the link of the row undoes its change
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPendingPagesModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    struct Row
    {
        PDFInteger page = -1;
        QString title;          ///< "Page 9"
        QString description;    ///< "90° right · portrait → landscape", "A4 portrait"
        QString iconName;       ///< Icon of the tile, empty = the text of the tile
        QString tileText;       ///< Text of the tile ("4", the label of the page)
        PDFComponentPainter::TileRowTone tone = PDFComponentPainter::TileRowTone::Accent;
        QString linkText;       ///< "Undo", "Keep"

        bool operator==(const Row&) const = default;
    };

    explicit PDFAppToolPendingPagesModel(QObject* parent);
    virtual ~PDFAppToolPendingPagesModel() override;

    /// The rows (a reset, when they differ)
    void setRows(std::vector<Row> rows);

    /// Page of the row, -1 for an invalid row
    PDFInteger getPage(int row) const;

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;

private:
    std::vector<Row> m_rows;
};

/// The common part of Rotate Pages and Remove Pages (389:20177, 388:20614): the pages the
/// tool works on are the result of the page selection of the left panel (the parameter
/// pages of the model, PDFDocumentToolPageSelection decides them as the core does); a
/// click on a tile of the grid adds or takes away its page (Shift adds the pages from the
/// tile clicked before), the list of the pending changes and the tiles follow the model.
/// At the entry the pages selected in the thumbnails are the selection, else the current
/// page (no frame: never every page at once - 388:21466 is the refusal of every page).
///
/// Ownership: the shell owns this GUI and the parts; the grid is the canvas page (the view
/// takes it during the mode), the pending model belongs to this GUI (the pending list,
/// built again after a change of the language, only shows it). Every part is held in a
/// QPointer. A change of the pages writes the model (one undo step): the model announces
/// it, this GUI updates the parts.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageMarkingToolGUI : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    explicit PDFAppPageMarkingToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppPageMarkingToolGUI() override;

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onDocumentChanged() override;

    /// The pages of the selection (indices from 0, sorted)
    const std::vector<PDFInteger>& getTargetPages() const { return m_targets; }
    bool isTarget(PDFInteger page) const;

    /// Pages of the shown document
    PDFInteger getPageCount() const;

    /// A click on the tile of the page (the grid, Space): the page is added or taken away;
    /// Shift adds the pages from the page clicked before
    void clickPage(int page, Qt::KeyboardModifiers modifiers);

    /// The pages of the selection: one undo step (Keep, Keep All Pages)
    void setTargetPages(const std::vector<PDFInteger>& pages);

    /// Parts (nullptr = none)
    PDFAppToolPageGrid* getGrid() const { return m_grid.data(); }
    PDFPageSelection* getPageSelection() const { return m_pageSelection.data(); }
    PDFAppToolPendingList* getPendingList() const { return m_pendingList.data(); }
    PDFAppToolPendingPagesModel* getPendingModel() const { return m_pendingModel.data(); }
    PDFAppParagraph* getLeftHint() const { return m_leftHint.data(); }

protected:
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;

    /// The page of the tiles (104 x 104, 82 x 112) and the texts of the parts
    virtual QSize getSlotSize() const = 0;
    virtual QString getSelectionLabel() const = 0;
    virtual QString getLeftHintText() const;
    virtual QString getPendingTitle() const = 0;
    virtual QString getGridHeaderText() const = 0;
    virtual QString getGridHintText() const = 0;

    /// Pages with a pending change (indices from 0, sorted) and their rows and tiles
    virtual std::vector<PDFInteger> getPendingPages() const = 0;
    virtual PDFAppToolPendingPagesModel::Row createPendingRow(PDFInteger page) const = 0;
    virtual PDFAppToolPageGridModel::Tile createTile(PDFInteger page) const = 0;

    /// The link of a pending row (Undo, Keep), Delete on a tile
    virtual void onPendingLinkActivated(PDFInteger page) = 0;
    virtual void onDeleteRequested(PDFInteger page);

    /// The panels of the tool show the current state (the summary, the toolbar)
    virtual void updatePanels();

    /// The shell has another state (Deciding: nothing can be chosen)
    virtual void onShellStateChanged(pdf::PDFDocumentToolShell::State state);

    /// Everything, which shows the pages (also after a change of the language)
    void updateAll();

    /// The document shown in the tab (nullptr = none)
    PDFDocumentPointer getShownDocument() const;

    /// The page of the document of the tab (nullptr = none)
    const PDFPage* getPage(PDFInteger page) const;

    /// "portrait" / "landscape" of a size
    static QString getOrientationText(const QSizeF& size);

private:
    void updateTargets();
    void onGridPageClicked(int page, Qt::KeyboardModifiers modifiers);
    void onGridDeleteRequested(int page);
    void onPendingLink(const QModelIndex& index);
    void onShellStateChangedSlot(pdf::PDFDocumentToolShell::State state);

    QPointer<PDFAppToolPageGrid> m_grid;
    QPointer<PDFPageSelection> m_pageSelection;
    QPointer<PDFAppToolPageSelectionBinder> m_binder;
    QPointer<PDFAppToolPendingList> m_pendingList;
    QPointer<PDFAppToolPendingPagesModel> m_pendingModel;
    QPointer<PDFAppParagraph> m_leftHint;
    std::vector<PDFInteger> m_targets;
    std::vector<char> m_isTarget;
    int m_anchorPage = -1;
    bool m_isEntryModeApplied = false;
    bool m_isShellConnected = false;
};

/// Rotate Pages (pages.rotate, 389:20177, 389:20910, 389:21590): the buttons turn the pages
/// of the selection, every click by another 90°; the turns are the parameter plan of the
/// model (Undo / Redo of the tool), the GUI always runs the plan. The tiles show the pages
/// turned, as they will be saved; Reset Rotation gives a page the rotation 0 (Q9).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIRotatePages : public PDFAppPageMarkingToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPageMarkingToolGUI;

public:
    explicit PDFAppToolGUIRotatePages(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIRotatePages() override;

    /// Turns of the pages, which will be saved (relative to their rotations now)
    const std::map<PDFInteger, PageRotation>& getTurns() const { return m_turns; }

    /// The buttons of the toolbar
    void rotateLeft();
    void rotateRight();
    void rotateHalf();
    void resetRotation();

    QToolButton* getRotateLeftButton() const { return m_rotateLeft.data(); }
    QToolButton* getRotateRightButton() const { return m_rotateRight.data(); }
    QToolButton* getRotateHalfButton() const { return m_rotateHalf.data(); }
    QToolButton* getResetButton() const { return m_reset.data(); }
    PDFKeyValueList* getSummary() const { return m_summary.data(); }
    PDFAppToolCallout* getCallout() const { return m_callout.data(); }

    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onDocumentChanged() override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

    virtual QSize getSlotSize() const override;
    virtual QString getSelectionLabel() const override;
    virtual QString getLeftHintText() const override;
    virtual QString getPendingTitle() const override;
    virtual QString getGridHeaderText() const override;
    virtual QString getGridHintText() const override;
    virtual std::vector<PDFInteger> getPendingPages() const override;
    virtual PDFAppToolPendingPagesModel::Row createPendingRow(PDFInteger page) const override;
    virtual PDFAppToolPageGridModel::Tile createTile(PDFInteger page) const override;
    virtual void onPendingLinkActivated(PDFInteger page) override;
    virtual void updatePanels() override;
    virtual void onShellStateChanged(pdf::PDFDocumentToolShell::State state) override;

private:
    /// The turns of the selection by \p turn (Reset: to the rotation 0) as one undo step
    void turnTargets(PageRotation turn, bool isReset);
    void writeTurns(const std::map<PDFInteger, PageRotation>& turns);
    void readTurns();

    /// "90° right" of all turns, "Mixed" (K14), empty for none
    QString getRotationText() const;

    /// The badge of a turn
    static QString getTurnIconName(PageRotation turn);

    std::map<PDFInteger, PageRotation> m_turns;
    QPointer<QToolButton> m_rotateLeft;
    QPointer<QToolButton> m_rotateRight;
    QPointer<QToolButton> m_rotateHalf;
    QPointer<QToolButton> m_reset;
    QPointer<PDFKeyValueList> m_summary;
    QPointer<PDFAppToolCallout> m_callout;
    bool m_isDeciding = false;
};

/// Remove Pages (pages.remove, 388:20614, 388:21466, 388:22315): the pages of the selection
/// are marked for removal; Keep of a row and Keep All Pages take the marks away, Delete marks
/// the current tile. Every page marked is refused (388:21466).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIRemovePages : public PDFAppPageMarkingToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPageMarkingToolGUI;

public:
    explicit PDFAppToolGUIRemovePages(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIRemovePages() override;

    /// Keep All Pages
    void keepAllPages();

    /// Every page is marked (388:21466)
    bool isEveryPageMarked() const;

    QToolButton* getKeepAllButton() const { return m_keepAll.data(); }
    PDFKeyValueList* getSummary() const { return m_summary.data(); }
    PDFAppToolCallout* getAllPagesCallout() const { return m_allPagesCallout.data(); }
    PDFAppParagraph* getNote() const { return m_note.data(); }

    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

    virtual QSize getSlotSize() const override;
    virtual QString getSelectionLabel() const override;
    virtual QString getPendingTitle() const override;
    virtual QString getGridHeaderText() const override;
    virtual QString getGridHintText() const override;
    virtual std::vector<PDFInteger> getPendingPages() const override;
    virtual PDFAppToolPendingPagesModel::Row createPendingRow(PDFInteger page) const override;
    virtual PDFAppToolPageGridModel::Tile createTile(PDFInteger page) const override;
    virtual void onPendingLinkActivated(PDFInteger page) override;
    virtual void onDeleteRequested(PDFInteger page) override;
    virtual void updatePanels() override;
    virtual void onShellStateChanged(pdf::PDFDocumentToolShell::State state) override;

private:
    QPointer<QToolButton> m_keepAll;
    QPointer<PDFKeyValueList> m_summary;
    QPointer<PDFAppToolCallout> m_allPagesCallout;
    QPointer<PDFAppParagraph> m_note;
    bool m_isDeciding = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_PAGES_H
