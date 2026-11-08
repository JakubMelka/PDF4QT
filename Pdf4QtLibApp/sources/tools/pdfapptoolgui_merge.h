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

#ifndef PDFAPPTOOLGUI_MERGE_H
#define PDFAPPTOOLGUI_MERGE_H

#include "pdfappglobal.h"
#include "pdfapppassworddialog.h"
#include "pdfappworkspace.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolmergeview.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolmergemodel.h"
#include "pdfapptoolpagegrid.h"
#include "pdfdocumenttoolgui.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <map>
#include <vector>

class QAction;
class QMimeData;
class QPushButton;
class QTimer;
class QToolButton;

namespace pdf
{

class PDFAppDocument;
class PDFAppParagraph;
class PDFAppPasswordDialog;
class PDFAppToolCallout;
class PDFAppToolCard;
class PDFAppToolInputList;
class PDFAppToolMergeCanvas;
class PDFAppToolPanelPopup;
class PDFAppToolSectionHeader;
class PDFAppToolTileSizeMenu;
class PDFKeyValueList;
class PDFLabel;
class PDFTag;
class PDFTileRow;

/// The stage of Merge without inputs (pages-merge-empty 378:1657, drop-zone 378:1827): a dashed
/// card 540 wide (bg/surface, border/strong dashed, radius 16) with the icon tile layers 44, "Add
/// the files to merge" Semi Bold 16, the line Regular 12, the Primary Add Files… and the group
/// "Open documents" - a row of each ready document of the window (the tile tab, the name, the
/// Tag "unsaved changes", the numbers, the Secondary Add). Built again by the GUI when the open
/// documents change; it only says what was chosen (the signals may delete it: emitted last).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolMergeEmptyState : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int CARD_WIDTH = 540;
    static constexpr int RADIUS = 16;
    static constexpr int PADDING_TOP = 29;     ///< Padding 28 + the border
    static constexpr int PADDING_SIDE = 29;
    static constexpr int PADDING_BOTTOM = 25;  ///< Padding 24 + the border
    static constexpr int GAP = 12;
    static constexpr int ICON_TILE = 44;
    static constexpr int ROW_HEIGHT = 52;

    /// An open document, which can be added
    struct Document
    {
        QString documentId;
        QString name;
        QString details;        ///< "9 pages · 15.3 MB", "3 pages · the current unsaved state is used"
        bool isModified = false;
    };

    explicit PDFAppToolMergeEmptyState(const std::vector<Document>& documents, QWidget* parent);
    virtual ~PDFAppToolMergeEmptyState() override;

    QPushButton* getAddFilesButton() const { return m_addFilesButton.data(); }
    std::vector<QPushButton*> getAddButtons() const;
    QWidget* getCard() const { return m_card.data(); }

Q_SIGNALS:
    void addFilesRequested();
    void addDocumentRequested(const QString& documentId);

private:
    void onAddClicked();

    QPointer<QWidget> m_card;
    QPointer<QPushButton> m_addFilesButton;
    std::vector<QPointer<QPushButton>> m_addButtons;
};

/// Merge (pages.merge; Figma 378:1049 … 412:32654, 1390:34070, 1370:34102; plan of step 2 of
/// phase 2, 4.4-4.6, K11-K16, K21-K24, K29, K36, K39, K40, Q5, Q9, Q11, Q17) in its own tab:
///  - the panel Files: the inputs of the list of the tab in their order with their states,
///    links (Enter password…, Remove file) and the drag of a file (the order of the result);
///  - the toolbar: Add Files (Files… Ctrl+I, Paste image Ctrl+V, the open documents), Rotate
///    Left / Right, Clone, Remove, Sort (File name, File type, Page number, Reverse order),
///    Outline (Document parts, Join outlines, No outline), Optimize (Off, Prefer quality,
///    Minimum size);
///  - the canvas: the pages of the result in groups (K14) or Details (408:32003), the size of
///    the tiles (Size: Small / Medium / Large, the shared setting of the page grids), the drop
///    zone without inputs (378:1657); a drop of files anywhere in the tab adds them;
///  - the right panel: the callout of the files, which need attention, Selected page, Result
///    (Pages, Outline, Images, Estimated size) and the note of the mode of the outline.
///
/// Values and Undo / Redo: the parameters "inputs" (the ids of the list) and "plan"
/// (PDFAppToolMergePlan) are one undo step of each change of the user; Undo / Redo set the
/// inputs of the list (removed inputs are parked by the list, the parked inputs, which no step
/// of the history names, are dropped) and the pages. The first values follow the list without
/// an undo step (K21: Undo is disabled at the first opening). The run gets only the usable
/// inputs (prepareEnvironment, getRunParameters; R16) and runs when every input can be used
/// and at least two are (K22).
///
/// Ownership (checklist point 1): the shell owns this GUI and the parts; the parts are held by
/// QPointer (they are made again after a change of the language). The list of the inputs
/// belongs to the tab (QPointer). The password dialog is new + QPointer (WA_DeleteOnClose).
/// The key widget of the host gets the actions Ctrl+I / Ctrl+V and the drop of files while
/// this GUI lives (the destructor takes them back).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMergeToolGUI : public PDFDocumentToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFDocumentToolGUI;

public:
    explicit PDFAppMergeToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppMergeToolGUI() override;

    /// Registers the tool and its GUI (PDFAppBuiltinTools::registerAll)
    static void registerTools();

    // ------------------------------------------------------------ parts

    virtual QWidget* createToolActions(QWidget* parent) override;
    virtual QWidget* createRightPanel(QWidget* parent) override;
    virtual QWidget* createLeftPanel(QWidget* parent) override;
    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual void setContentEnabled(bool isEnabled) override;

    // ------------------------------------------------------------ texts

    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QString getRunningCardTitle(int percentage, const QString& stepText) const override;
    virtual QString getRunningCardStepText(const QString& stepText) const override;
    virtual QString getRunningTimeText(int seconds) const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual QString getOwnTabLossText() const override;
    virtual QString getLockedGlanceText() const override;
    virtual QString getOwnTabPageText() const override;
    virtual QString getOwnTabZoomText() const override;

    // ------------------------------------------------------------ flow

    virtual QVariantMap getRunParameters() const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;

    // ------------------------------------------------------------ actions of the user

    /// Adds the files at the end (one undo step); refused ones are said in the status bar
    void addFiles(const QStringList& filePaths);

    /// Files… (Ctrl+I): the dialog of the files, then addFiles
    void addFilesFromDialog();

    /// Paste image (Ctrl+V): the image of the clipboard at the end
    void pasteImage();

    /// Adds a snapshot of the open document (R11)
    void addOpenDocument(const QString& documentId);

    /// Removes the input (one undo step; the list parks it)
    void removeInput(const QString& inputId);

    /// Moves the input in front of the input at \p targetIndex (the count = to the end); its
    /// pages go with it (the pages are ordered by the order of the files)
    void moveInput(const QString& inputId, int targetIndex);

    /// The selected pages: turned, cloned, removed, moved (each one undo step)
    void rotateSelection(bool isClockwise);
    void rotateLeft();
    void rotateRight();
    void cloneSelection();
    void removeSelection();
    void moveSelection(int delta);
    void moveSelectionTo(const std::vector<int>& rows, int targetRow);

    /// Sort (378:1879): the files by their names (natural order), by their type (PDF documents
    /// first), the pages by their original numbers (K13), the current order reversed
    void sortByName();
    void sortByType();
    void sortByPageNumber();
    void reverseOrder();

    /// Opens the password dialog of the input (398:27726, 1390:34569)
    void openPasswordDialog(const QString& inputId);

    /// Accepts a drop of files (the drop anywhere in the tab); \returns true for files
    bool dropFiles(const QMimeData* mimeData);

    /// Thumbnails / Details, the size of the tiles
    void setDetailsShown(bool isDetailsShown);
    void setTileSize(PDFAppToolPageGrid::TileSize size);

    // ------------------------------------------------------------ state (tests)

    const PDFAppToolMergePlan& getPlan() const { return m_plan; }
    std::vector<int> getSelectedRows() const;
    void selectRows(const std::vector<int>& rows);
    PDFAppToolMergeCanvas* getCanvas() const { return m_canvas.data(); }
    PDFAppToolInputFilesPanel* getFilesPanel() const { return m_filesPanel.data(); }
    PDFAppToolCallout* getAttentionCallout() const { return m_attentionCallout.data(); }
    QWidget* getSelectedSection() const { return m_selectedSection.data(); }
    PDFTag* getSelectedTag() const { return m_selectedTag.data(); }
    PDFKeyValueList* getSelectedValues() const { return m_selectedValues.data(); }
    QWidget* getResultSection() const { return m_resultSection.data(); }
    PDFKeyValueList* getResultValues() const { return m_resultValues.data(); }
    PDFAppParagraph* getOutlineNote() const { return m_outlineNote.data(); }
    QWidget* getHowSection() const { return m_howSection.data(); }
    PDFAppPasswordDialog* getPasswordDialog() const { return m_passwordDialog.data(); }
    QToolButton* getAction(const QString& name) const;
    PDFAppToolPanelPopup* getPanel(const QString& name) const;
    PDFTileRow* getPanelRow(const QString& name) const;
    PDFAppToolTileSizeMenu* getTileSizeMenu() const { return m_tileSizeMenu.data(); }
    PDFAppToolMergeEmptyState* getEmptyState() const;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// A page of the plan by its identity: the input, the page and its occurrence (the
    /// selection survives a change, which moves the pages)
    struct PageKey
    {
        QString inputId;
        PDFInteger pageIndex = -1;
        int occurrence = 0;

        bool operator==(const PageKey&) const = default;
    };

    // The values and the list
    void connectList();
    void initializeValues();
    void readValues();
    void applyChange(const QStringList& inputIds, const PDFAppToolMergePlan& plan, const std::vector<size_t>& selectedItems);
    QStringList getModelInputIds() const;
    std::vector<PDFInteger> getKnownPageCounts(const QStringList& inputIds) const;
    std::vector<size_t> getSelectedItems() const;
    std::vector<PageKey> getSelectedKeys() const;
    std::vector<int> getRowsOfKeys(const std::vector<PageKey>& keys) const;
    QStringList getUsableInputIds() const;
    void addResults(const std::vector<PDFAppToolInputList::AddResult>& results, const QStringList& refusedTexts);

    // The slots of the list, the parts and the shell
    void onListInputsChanged();
    void onListInputChanged(const QString& inputId);
    void onReadQueued();
    void onListDestroyed();
    void onShellStateChanged(pdf::PDFDocumentToolShell::State state);
    void onSelectionChanged();
    void onFileRowClicked(const QString& inputId);
    void onFileLinkActivated(const QString& inputId);
    void onPasswordRemoveRequested(const QString& inputId);
    void onViewSwitched(bool isDetailsShown);
    void onTileSizeChosen(pdf::PDFAppToolPageGrid::TileSize size);
    void onTileSizeButtonClicked();
    void onTileSizePanelClosed();
    void onProgressTimer();
    void onAddFilesRowClicked();
    void onPasteRowClicked();
    void onOpenDocumentRowClicked();
    void onSortRowClicked();
    void onOutlineRowClicked();
    void onOptimizeRowClicked();
    void onEmptyStateAddFiles();
    void onEmptyStateAddDocument(const QString& documentId);
    void onRegistryChanged();

    // The parts
    void updateAll();
    void updateFiles();
    void updateCanvas();
    void updateRightPanel();
    void updateToolbar();
    void updateSelectedPage();
    void updateEmptyState();
    void updateOpenDocumentsRows();
    void updateEditable();
    std::vector<PDFAppToolMergeEmptyState::Document> getOpenDocuments() const;
    std::vector<PDFAppDocument*> getOpenAppDocuments() const;
    PDFAppToolInputRow::Content createRowContent(const PDFAppToolInput& input, int index) const;
    QString getRunInResultText(const QString& inputId) const;
    QString getOutlineValue() const;
    QString getOptimizeValue() const;
    QString getOutlineNoteText() const;
    QString getAttentionText() const;
    QString getParameter(const char* name) const;
    void setParameter(const char* name, const QString& value);
    void closePanel(const QString& name);
    bool isEditable() const;
    bool isTabOpen(const QString& documentId) const;

    QPointer<PDFAppToolInputList> m_list;
    PDFAppToolMergePlan m_plan;
    QPointer<PDFAppToolMergeCanvas> m_canvas;
    QPointer<PDFAppToolInputFilesPanel> m_filesPanel;
    QPointer<PDFAppToolCallout> m_attentionCallout;
    QPointer<QWidget> m_selectedSection;
    QPointer<PDFTag> m_selectedTag;
    QPointer<PDFKeyValueList> m_selectedValues;
    QPointer<QWidget> m_resultSection;
    QPointer<PDFKeyValueList> m_resultValues;
    QPointer<PDFAppParagraph> m_outlineNote;
    QPointer<QWidget> m_howSection;
    std::map<QString, QPointer<QToolButton>> m_actions;
    std::map<QString, QPointer<PDFAppToolPanelPopup>> m_panels;
    std::map<QString, QPointer<PDFTileRow>> m_panelRows;
    std::vector<QPointer<PDFTileRow>> m_openDocumentRows;
    QPointer<PDFLabel> m_openDocumentsCaption;
    QPointer<PDFAppToolTileSizeMenu> m_tileSizeMenu;
    QPointer<PDFAppPasswordDialog> m_passwordDialog;
    QPointer<QTimer> m_progressTimer;
    QPointer<QWidget> m_keyWidget;
    QPointer<QAction> m_addFilesAction;
    QPointer<QAction> m_pasteAction;
    std::vector<size_t> m_pendingSelection;
    QStringList m_leftOutInputs;
    QString m_currentInput;
    PDFAppToolPageGrid::TileSize m_tileSize = PDFAppToolPageGrid::TileSize::Medium;
    bool m_isTileSizeRead = false;
    bool m_hasPendingSelection = false;
    bool m_isValuesInitialized = false;
    bool m_isChangingList = false;
    bool m_isReadQueued = false;      ///< A finished reading waits for its update (one update for the readings of one pass of the event loop)
    bool m_isRunning = false;
    bool m_isDeciding = false;
    bool m_isDetailsShown = false;
    bool m_isShellConnected = false;
    bool m_isRegistryConnected = false;
    bool m_wasDropAccepted = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_MERGE_H
