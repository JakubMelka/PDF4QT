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

#ifndef PDFAPPTOOLGUI_MANAGE_H
#define PDFAPPTOOLGUI_MANAGE_H

#include "pdfappglobal.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappmanagecanvas.h"
#include "pdfappmanagedialogs.h"
#include "pdfappmanagepanels.h"
#include "pdfapppassworddialog.h"
#include "pdfappassemblyloader.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolpagegrid.h"
#include "pdfdocumenttoolgui.h"
#include "pdfdocumenttoolshell.h"

#include "pdfdocumentassembly.h"
#include "pdfdocumenttool_split.h"

#include <QPointer>
#include <QStringList>

#include <map>
#include <set>
#include <vector>

class QAction;
class QMenu;
class QMimeData;
class QTimer;
class QToolButton;

namespace pdf
{

class PDFAppAssemblyLoadResult;
class PDFAppAssemblyLoader;
class PDFAppMainWindow;
class PDFAppManageDialog;
class PDFAppManageResultPanel;
class PDFAppToolTab;
class PDFAppManageSourcesPanel;
class PDFAppPasswordDialog;
class PDFAppToolInputList;
class PDFAppToolOutputFilesSection;
class PDFAppToolPanelPopup;
class PDFAppToolTileSizeMenu;
class PDFMessageBox;
class PDFTileRow;

/// The GUI of Manage Pages (plan of step 3b of phase 2, chapter 2, 4.5, QB1 B, QB7 b, RB2, RB3;
/// package M3): one GUI, two hosts.
///  - Its own tab (started without a document: Home, Tools, an assembly file): the assembly of
///    sources, the outputs One PDF, per group, per page (387:12569 ... 409:32739); the sources are
///    never changed; the tab stays after a result (isTabKeptAfterResult, K31); Close instead of
///    Cancel (K2); the close asks "Save the assembly …?" (askOwnTabClose, 405:30894).
///  - The tab of a document (started over a document: the panel Tools, Thumbnails Move pages,
///    Then continue with; 1479:34632, 1479:35631): the document is the source A ("this document"),
///    the only output is This document (QB7 b; "Open as an Assembly" opens the own tab with its
///    snapshot), Cancel closes the tool, no assembly file, no Group / Ungroup / Regroup / Rename
///    (coordinator); after a change of the document the assembly follows its page objects (RB2,
///    PDFDocumentAssembly::mapToDocument) - the current value and every undo step - and the status
///    says "N pages were added to the document and placed at the end".
///
/// Values and Undo / Redo (4.2, RB3 d, RB7): the parameter "assembly" (an immutable value) and
/// "inputs" (the ids of the list of the inputs of the sources) are one undo step of each operation
/// with its text ("Undo Group"); every operation is an operation of the core (PDFDocumentAssembly),
/// the GUI only chains them. The checkpoints are a state of the GUI (RB7; restored as an undo step,
/// B22). The sources are read by a PDFAppToolInputList (the tab's in the own tab, an own one in the
/// tab of a document): an added file becomes a source, when it is read (a pending addition).
///
/// Ownership (checklist point 1): the shell owns this GUI and the parts (QPointer here, made again
/// after a change of the language): the canvas (PDFAppManageCanvas), the panels, the toolbar
/// actions. The list of the own tab belongs to the tab (QPointer); the list of the tab of a
/// document, the loader of an assembly file and the output files section are children of this GUI.
/// A dialog is new + QPointer with the window as its parent (WA_DeleteOnClose); one dialog at a
/// time. The actions of the keys live on the key widget of the host while this GUI does (the
/// destructor takes them back). Every signal of a part runs code of this GUI, which may close the
/// tab: each update is guarded (emit last, QPointer).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageToolGUI : public PDFDocumentToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFDocumentToolGUI;

public:
    /// Widths of the panels of the toolbar (387:13288, 387:14220, 396:26763, 409:32023)
    static constexpr int ADD_PANEL_WIDTH = 270;
    static constexpr int SELECT_PANEL_WIDTH = 250;
    static constexpr int GROUP_PANEL_WIDTH = 290;
    static constexpr int SORT_PANEL_WIDTH = 250;
    static constexpr int MORE_PANEL_WIDTH = 270;
    static constexpr int OUTPUT_PANEL_WIDTH = 320;
    static constexpr int ASSEMBLY_PANEL_WIDTH = 320;

    /// Interval of the polling of the reading of the sources (ms)
    static constexpr int PROGRESS_INTERVAL = 100;

    /// Most rows of the removed items in the expanded bin (the newest first)
    static constexpr int MAXIMUM_REMOVED_ROWS = 100;

    /// The parts, which F6 / Shift+F6 go through (405:31588): the toolbar, the left panel, the
    /// pages, the right panel
    static constexpr int PART_COUNT = 4;

    /// MIME type of the items in the clipboard (4.6, B18): the instance of the assembly and the ids
    static constexpr const char* CLIPBOARD_MIME_TYPE = "application/x-pdf4qt-assembly-items";

    explicit PDFAppManageToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppManageToolGUI() override;

    /// The tool runs in the tab of a document (not in its own tab)
    bool isDocumentHost() const;

    /// 1 to 1000 PDF files and images (K41 of step 3, plan 3b 1 point 7)
    virtual PDFAppToolInputRules getInputRules() const override;

    /// The own tab stays open after a result (K31, 396:27563)
    virtual bool isTabKeptAfterResult() const override;

    /// Close in the own tab (K2), Cancel in the tab of a document (RB3 f)
    virtual QString getCancelText() const override;

    /// In the tab of a document the GUI shows the missing Assemble itself (1479:35631, owner answer
    /// 1 of 12. 11.): the shell does not refuse it at the entry
    virtual bool handlesPermissionRefusal() const override;

    // ------------------------------------------------------------ parts

    virtual QWidget* createToolActions(QWidget* parent) override;
    virtual QWidget* createLeftPanel(QWidget* parent) override;
    virtual QWidget* createRightPanel(QWidget* parent) override;
    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual void setContentEnabled(bool isEnabled) override;

    // ------------------------------------------------------------ texts

    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QString getRunningCardTitle(int percentage, const QString& stepText) const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual QString getOwnTabTitle() const override;
    virtual QString getOwnTabLossText() const override;
    virtual QString getLockedGlanceText() const override;
    virtual QString getOwnTabPageText() const override;
    virtual QString getOwnTabZoomText() const override;
    virtual PDFAppToolCloseAnswer askOwnTabClose() override;

    // ------------------------------------------------------------ flow

    virtual QVariantMap getRunParameters() const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onDocumentChanged() override;
    virtual void retranslate() override;

    // ------------------------------------------------------------ the sources (Add, 387:13288)

    /// Files… (Ctrl+I) and a drop: the files become sources, their pages go after the selection
    void addFiles(const QStringList& filePaths);
    void addFilesFromDialog();

    /// Pages of a PDF…: the dialog of the file, then the dialog of its pages (387:15926)
    void addPagesFromDialog();
    void addPagesOfFile(const QString& filePath);

    /// Blank page (Ctrl+Shift+I), Paste image, an open document (its snapshot, R11)
    void insertBlankPage();
    void pasteImage();
    void addOpenDocument(const QString& documentId);

    /// A drop of files anywhere in the tab; \returns true for files
    bool dropFiles(const QMimeData* mimeData);

    // ------------------------------------------------------------ the selection (Select)

    void selectAll();
    void selectNone();
    void selectInvert();
    void selectOdd();
    void selectEven();
    void selectPortrait();
    void selectLandscape();
    void selectSearchResults();
    void select(PDFDocumentAssembly::SelectRule rule, const PDFDocumentAssembly::SelectSettings& settings);
    void openRangeDialog();

    /// The selected items (both views)
    const PDFDocumentAssembly::Selection& getSelection() const;
    void setSelection(const PDFDocumentAssembly::Selection& selection);

    // ------------------------------------------------------------ operations (one undo step each)

    void group();
    void ungroup();
    void regroup(PDFDocumentAssembly::RegroupMode mode, const PDFDocumentAssembly::RegroupSettings& settings);
    void openEveryDialog();
    void openNumbersDialog();
    void openBookmarksDialog();
    void sort(PDFDocumentAssembly::SortKey key);
    void rotateLeft();
    void rotateRight();
    void resetRotation();
    void cloneSelection();
    void removeSelection();
    void moveSelection(int delta);
    void moveSelectionBefore(const QString& beforeItemId);
    void openMoveDialog();
    void rename(const QString& name);
    void openRenameDialog();
    void openReplaceDialog();
    void crop(const QMarginsF& margins, bool isSameSource);
    void openCropDialog();
    void openPropertiesDialog();
    void restoreItem(const QString& removedItemId);
    void restoreAll();

    /// Cut (into the bin at once, B53), Copy, Paste (Ctrl+V: the items of the clipboard of this
    /// assembly, else its image, K7; items of another assembly are refused, B18)
    void cut();
    void copy();
    void paste();

    /// Checkpoints (B22, B23): saved with a name (not an undo step), restored as an undo step
    void saveCheckpoint(const QString& name);
    void openCheckpointDialog();
    void restoreCheckpoint(const QString& checkpointId);
    const std::vector<PDFDocumentAssembly::Checkpoint>& getCheckpoints() const { return m_checkpoints; }

    // ------------------------------------------------------------ output (396:26763)

    void setOutput(const QString& output);
    void setOutline(const QString& outline);
    void setImages(const QString& images);
    QString getOutput() const;

    /// Show document titles (387:14220): the titles of the documents instead of the file names
    void setDocumentTitlesShown(bool isShown);
    bool areDocumentTitlesShown() const { return m_areDocumentTitlesShown; }

    // ------------------------------------------------------------ the assembly file (409:32023)

    /// Save Assembly (Ctrl+S): to its file, the first time Save Assembly As… \returns true, when
    /// it was saved
    bool saveAssembly();
    bool saveAssemblyAs();
    bool saveAssemblyTo(const QString& filePath, QString* errorMessage);

    /// Open Assembly… (Ctrl+Shift+O), a recent assembly, an assembly file of the window
    /// (PDFAppMainWindow::openAssembly): read by PDFAppAssemblyLoader; the content replaces the
    /// assembly as one undo step (B24: only after a successful read)
    void openAssemblyFromDialog();
    bool openAssemblyFile(const QString& filePath);
    void applyLoadResult(const PDFAppAssemblyLoadResult& result);

    /// Open Assembly / Open Without Them of 387:16980 (and a file, whose sources are all usable):
    /// the content of PDFAppAssemblyLoader::createContent replaces the assembly as one undo step
    void applyLoadedContent(const PDFAppAssemblyLoadResult& result);

    /// The own tab of Manage Pages of the window, which has the assembly file open (the canonical
    /// path) - opened or saved, being loaded, or with its sources dialog - nullptr = none
    /// (PDFAppMainWindow::openAssembly activates it instead of a new tab)
    static PDFAppToolTab* findAssemblyTab(const PDFAppMainWindow* window, const QString& filePath);

    /// The status of a second Locate… / Open From There of 387:16980, while the loader checks a file
    static QString getLocateBusyText();

    const QString& getAssemblyPath() const { return m_assemblyPath; }
    bool isAssemblyModified() const;

    /// Open as an Assembly (1479:34632): the own tab with a snapshot of the document as its source
    void openAsAssembly();

    // ------------------------------------------------------------ state (tests)

    const PDFDocumentAssembly& getAssembly() const { return m_assembly; }
    PDFAppToolInputList* getSourceList() const { return m_list.data(); }
    QString getInputOfSource(const QString& sourceId) const;
    PDFAppManageCanvas* getCanvas() const { return m_canvas.data(); }
    PDFAppManageSourcesPanel* getSourcesPanel() const { return m_sourcesPanel.data(); }
    PDFAppManageResultPanel* getResultPanel() const { return m_resultPanel.data(); }
    PDFAppToolOutputFilesSection* getOutputFiles() const { return m_outputFiles.data(); }
    PDFAppToolTileSizeMenu* getTileSizeMenu() const { return m_tileSizeMenu.data(); }
    PDFAppManageDialog* getDialog() const { return m_dialog.data(); }
    PDFAppDocumentChoiceDialog* getCloseDialog() const { return m_closeDialog.data(); }
    PDFAppManageSourcesDialog* getSourcesDialog() const { return m_sourcesDialog.data(); }
    QMenu* getContextMenu() const { return m_contextMenu.data(); }
    QToolButton* getAction(const QString& name) const;
    PDFAppToolPanelPopup* getPanel(const QString& name) const;
    PDFTileRow* getPanelRow(const QString& name) const;
    QAction* getKeyAction(const QString& name) const;
    const QString& getStatusNote() const { return m_statusNote; }
    const std::set<PDFObjectReference>& getNewTargetPages() const { return m_newTargetPages; }

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// An added input, which becomes a source when it is read
    struct PendingAddition
    {
        enum class Kind
        {
            AllPages,       ///< Files…, a drop, an open document, Paste image: all pages after the selection
            ChoosePages,    ///< Pages of a PDF…: the dialog of its pages
            ReplacePages,   ///< Replace selection…: the dialog of its pages
            Bind            ///< A source of a read assembly file: only its input
        };

        QString inputId;
        Kind kind = Kind::AllPages;
        PDFDocumentAssembly::Selection selection;   ///< The selection at the time of the request
        QString sourceId;                           ///< Bind: the source
        bool isInitial = false;                     ///< The inputs, which the tab came with (no undo step, K21)
    };

    /// The file of an instance of the assembly (PDFDocumentAssembly::getInstanceId): Open Assembly
    /// is an undo step, which brings a new instance - Undo brings back the assembly before it with
    /// its own file, checkpoints and saved state (Save never writes it into the opened file)
    struct FileState
    {
        QString path;
        PDFDocumentAssembly savedAssembly;
        std::vector<PDFDocumentAssembly::Checkpoint> checkpoints;
        size_t savedCheckpointCount = 0;
    };

    // The values and the list
    void connectParts();
    void initializeValues();
    void readValues();
    void followFileState();
    void syncList();
    QStringList getModelInputIds() const;
    QString getParameter(const char* name) const;
    void setParameter(const char* name, const QString& value);

    /// Writes the assembly (and the inputs) as one undo step with the text; the selection follows
    void applyAssembly(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Selection& selection, const QString& stepText, const QString& errorMessage);
    void applyEdit(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Edit& edit, const QString& stepText);

    // The sources
    void addResults(const std::vector<std::pair<QString, int>>& added, PendingAddition::Kind kind, const QStringList& refusedTexts);
    void queueProcessPending();
    void onProcessPending();
    PDFDocumentAssembly::Source createSource(const QString& inputId) const;
    QString findSourceOfInput(const QString& inputId) const;
    QString findSourceOfFile(const QString& filePath) const;
    std::vector<PDFAppManageSourceView> createSourceViews() const;
    QString getSourceDisplayName(int sourceIndex) const;
    void openAddPagesDialog(const QString& sourceKey, const QString& fileName, PDFInteger pageCount, bool isReplace);
    void openSourcesDialog(const PDFAppAssemblyLoadResult& result);
    void onSourcesDialogLocate(const QString& sourceId);
    void onSourcesDialogOpenFromThere(const QString& sourceId);
    void onSourcesDialogFinished();
    void openPasswordDialog(const QString& inputId);
    void onPasswordRemoveRequested(const QString& inputId);

    // Document host
    void updateDocumentMapping();
    PDFAppPermissionRequest createPermissionRequest() const;
    bool isDocumentChangeAllowed() const;
    QString getLockText() const;
    /// The document of the tab has more pages than an assembly (owner 2026-11-12): the reason,
    /// else empty; the tool is then not editable and Rearrange Pages is Disabled
    QString getPageLimitText() const;

    // Slots of the parts
    void onListInputsChanged();
    void onListInputChanged(const QString& inputId);
    void onListDestroyed();
    void onShellStateChanged(pdf::PDFDocumentToolShell::State state);
    void onCanvasSelectionChanged();
    void onCanvasMoveRequested(const pdf::PDFDocumentAssembly::Selection& selection, const QString& beforeItemId);
    void onCanvasContextMenu(const QPoint& globalPosition);
    void onCanvasSearchChanged(const QString& text);
    void onCanvasViewSwitched(bool isDetailsShown);
    void onSourceClicked(const QString& sourceId);
    void onSourceLinkActivated(const QString& sourceId);
    void onCalloutLinkActivated();
    void onProgressTimer();
    void onPanelRowClicked();
    void onOpenDocumentRowClicked();
    void onRecentRowClicked();
    void onTileSizeButtonClicked();
    void onTileSizePanelClosed();
    void onTileSizeChosen(pdf::PDFAppToolPageGrid::TileSize size);
    void onKeyAction();
    void onContextAction();
    void onRegistryChanged();
    void onEmptyStateAddFiles();
    void onEmptyStateAddDocument(const QString& documentId);
    void onEmptyStateOpenAssembly();
    void onEmptyStateOpenRecent(const QString& filePath);
    void onLoaderLoaded(const pdf::PDFAppAssemblyLoadResult& result);
    void onRangeChosen(const pdf::PDFDocumentAssembly::SelectSettings& settings);
    void onBookmarksChosen(const pdf::PDFDocumentAssembly::RegroupSettings& settings);
    void onPagesChosen(const QString& sourceKey, const std::vector<PDFInteger>& pages, bool isReplace);
    void onEveryChosen(int value);
    void onMoveChosen(int value);
    void onNumbersChosen(const QString& text);
    void onRenameChosen(const QString& name);
    void onCheckpointNameChosen(const QString& name);
    void onCropChosen(const QMarginsF& margins, bool isSameSource);

    // Actions by name (the rows of the menus, the keys, the context menu)
    void connectRows(const std::vector<PDFTileRow*>& rows);
    void runAction(const QString& name);
    bool isActionEnabled(const QString& name) const;
    void closePanels();
    void installKeys();
    void showDialog(PDFAppManageDialog* dialog);
    void focusNextPart();
    void focusPreviousPart();
    void focusPart(int delta);
    QWidget* getPart(int index) const;
    QWidget* getWindow() const;
    QWidget* getScrimTarget() const;
    void showMessage(const QString& text);

    // Update of the parts
    void updateAll();
    void updateCanvas();
    void updateSources();
    void updateRightPanel();
    void updateToolbar();
    void updateEmptyState();
    void updateOpenDocumentRows();
    void updateRecentRows();
    void updateEditable();
    bool isEditable() const;
    std::vector<PDFAppDocument*> getOpenAppDocuments() const;
    QString getOutputText() const;
    QString getOutlineText() const;
    QString getImagesText() const;
    int getOutputFileCount() const;
    int getSelectedPageCount() const;
    std::vector<PDFDocumentToolFileOutput::File> createOutputFiles() const;

    PDFDocumentAssembly m_assembly;
    PDFDocumentAssembly m_savedAssembly;                ///< The value written to the assembly file last (isEqual: no changes)
    std::vector<PDFDocumentAssembly::Checkpoint> m_checkpoints;
    size_t m_savedCheckpointCount = 0;
    std::map<QString, QString> m_sourceInputs;          ///< Input of the list of each source (sources are only added)
    std::vector<PendingAddition> m_pending;
    std::vector<PDFDocumentAssembly::Item> m_clipboardItems;    ///< The copied items by value (4.6); the clipboard names them by its token
    QString m_clipboardToken;
    std::set<PDFObjectReference> m_newTargetPages;
    PDFDocumentAssembly::Selection m_pendingSelection;
    QString m_assemblyPath;
    QString m_fileInstanceId;                           ///< The instance of the assembly, whose file state the members above hold
    std::map<QString, FileState> m_otherFileStates;     ///< The file states of the other instances of the undo history
    QString m_statusNote;
    quint64 m_documentVersion = 0;
    int m_nextCheckpoint = 1;

    QPointer<PDFAppToolInputList> m_list;
    QPointer<PDFAppToolInputList> m_ownList;
    QPointer<PDFAppAssemblyLoader> m_loader;
    QPointer<PDFAppToolOutputFilesSection> m_outputFiles;
    QPointer<PDFAppManageCanvas> m_canvas;
    QPointer<PDFAppManageSourcesPanel> m_sourcesPanel;
    QPointer<PDFAppManageResultPanel> m_resultPanel;
    QPointer<PDFAppToolTileSizeMenu> m_tileSizeMenu;
    QPointer<PDFAppManageDialog> m_dialog;
    QPointer<PDFAppDocumentChoiceDialog> m_closeDialog;
    QPointer<PDFAppManageSourcesDialog> m_sourcesDialog;
    QPointer<PDFAppPasswordDialog> m_passwordDialog;
    QPointer<QMenu> m_contextMenu;
    QPointer<QTimer> m_progressTimer;
    QPointer<QWidget> m_keyWidget;
    std::map<QString, QPointer<QToolButton>> m_actions;
    std::map<QString, QPointer<PDFAppToolPanelPopup>> m_panels;
    std::map<QString, QPointer<PDFTileRow>> m_panelRows;
    std::vector<QPointer<PDFTileRow>> m_openDocumentRows;
    std::vector<QPointer<PDFTileRow>> m_recentRows;
    std::map<QString, QPointer<QAction>> m_keyActions;
    PDFAppToolPageGrid::TileSize m_tileSize = PDFAppToolPageGrid::TileSize::Small;
    bool m_isValuesInitialized = false;
    bool m_isChangingList = false;
    bool m_isPendingQueued = false;
    bool m_hasPendingSelection = false;
    bool m_isRunning = false;
    bool m_isDeciding = false;
    bool m_isDetailsShown = false;
    bool m_isTileSizeRead = false;
    bool m_isShellConnected = false;
    bool m_isRegistryConnected = false;
    bool m_wasDropAccepted = false;
    bool m_areDocumentTitlesShown = false;
};

/// The registration of Manage Pages (plan 9.1): PDFAppBuiltinTools::registerAll calls it; the tool
/// (PDFDocumentToolManage of package M1) and this GUI
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIManage
{
public:
    PDFAppToolGUIManage() = delete;

    static void registerManageTools();
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_MANAGE_H
