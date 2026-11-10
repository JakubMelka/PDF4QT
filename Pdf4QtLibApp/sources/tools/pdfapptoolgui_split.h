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

#ifndef PDFAPPTOOLGUI_SPLIT_H
#define PDFAPPTOOLGUI_SPLIT_H

#include "pdfappglobal.h"
#include "pdfapptoolgui_pages.h"
#include "pdfapptooloutputfiles.h"
#include "pdfdocumenttool_split.h"
#include "pdfoutlinebreakpoints.h"

#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QStyledItemDelegate>

#include <set>

class QListView;

namespace pdf
{

class PDFAppSettingControl;
class PDFAppThumbnailListView;
class PDFAppThumbnailScheduler;
class PDFAppThumbnailsModel;
class PDFAppToolParameterModel;
class PDFDocumentToolRunner;
class PDFTextLink;
class PDFThumbnailDelegate;

/// A number of an Integer or Real parameter in a toolbar (component Toolbar field: "12 pages per
/// file" 383:3315 M13, "10 MB" 383:7775): the value Semi Bold 13 and its suffix, no arrows, as
/// wide as its texts. Enter or the loss of the focus writes the model (one undo step); a text,
/// which is no valid value, goes back to the value of the model. The field follows Undo and Redo.
///
/// Ownership: a child of the toolbar of the tool; the model is held by QPointer.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolNumberField : public PDFToolBarField
{
    Q_OBJECT

private:
    using BaseClass = PDFToolBarField;

public:
    explicit PDFAppToolNumberField(PDFAppToolParameterModel* model, const QString& parameter, const QString& suffix, QWidget* parent);
    virtual ~PDFAppToolNumberField() override;

    /// Writes the text of the field into the model (Enter, the loss of the focus)
    void commit();

    /// The text of a value of the parameter in the locale of the user interface
    QString formatValue(const QVariant& value) const;

private:
    void onParameterChanged(const QString& name);
    void readValue();

    QPointer<PDFAppToolParameterModel> m_model;
    QString m_parameter;
};

/// The bookmarks of the document, which may start a new file (Split At bookmarks 383:6771): a
/// row per bookmark of the outline (PDFOutlineBreakPoints) with its check box, its number in the
/// outline ("2.1"), its title and its page ("p. 8"), the children indented by 20. The checked
/// rows are the numbers of the value of the parameter bookmarks ("top-level" checks the
/// bookmarks of the top level). A value; GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSplitBookmarkModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    /// Roles of a row
    enum Role
    {
        NumberRole = Qt::UserRole + 1,  ///< QString: "2.1"
        LevelRole,                      ///< int: 0 = top level
        PageTextRole                    ///< QString: "p. 8", empty for a bookmark without a page
    };

    explicit PDFAppSplitBookmarkModel(QObject* parent);
    virtual ~PDFAppSplitBookmarkModel() override;

    /// The bookmarks of the document (a reset, when they differ)
    void setBookmarks(std::vector<PDFOutlineBreakPoints::Bookmark> bookmarks);
    const std::vector<PDFOutlineBreakPoints::Bookmark>& getBookmarks() const { return m_bookmarks; }

    /// The checked rows of the value of the parameter (one dataChanged of the changed rows)
    void setValue(const QString& value);

    /// The rows checked now (indices from 0)
    std::vector<int> getCheckedRows() const;

    /// The value of the parameter, which checks \p rows: the numbers from 1 with ranges
    /// ("1, 2, 5-7"), "top-level" when they are the bookmarks of the top level
    QString createValue(const std::vector<int>& rows) const;

    /// The rows checked by a value (invalid parts check nothing)
    std::vector<char> getCheckedByValue(const QString& value) const;

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    std::vector<PDFOutlineBreakPoints::Bookmark> m_bookmarks;
    std::vector<QString> m_numbers;
    std::vector<char> m_checked;
};

/// The row of a bookmark of 383:6771: 24 high, the check box 18 and the label 8 apart (Regular 13
/// text/primary), the page at the trailing edge (Regular 11 text/secondary), a child indented by
/// 20 per level; mirrored for right-to-left. GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSplitBookmarkDelegate : public QStyledItemDelegate
{
    Q_OBJECT

private:
    using BaseClass = QStyledItemDelegate;

public:
    static constexpr int ROW_HEIGHT = 24;
    static constexpr int ROW_GAP = 2;
    static constexpr int CHECK_SIZE = 18;
    static constexpr int CHECK_GAP = 8;
    static constexpr int INDENT = 20;
    static constexpr int MAXIMUM_INDENT_LEVEL = 8;
    static constexpr int PAGE_GAP = 6;

    explicit PDFAppSplitBookmarkDelegate(QObject* parent);

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    /// Rectangle of the check box of the row in \p rect (tests)
    static QRect getCheckRect(const QRect& rect, int level, bool isRightToLeft);
};

/// The row of a file of Split (Files to create 383:3315): the tile row of the pending list and the
/// line of its state below the description (Written 383:10232 green, "Same name as another file"
/// 383:9088 red; PDFAppToolPendingPagesModel::StatusRole): the row is 59 high then, the texts start
/// 8 below its top, the tile stays in the middle. GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSplitFileRowDelegate : public PDFTileRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFTileRowDelegate;

public:
    /// 383:9088 file-1: the padding 8, the tile 30 and its gap 10, the lines 15 + 1 + 13 + 1 + 13
    static constexpr int PADDING = 8;
    static constexpr int TILE = 30;
    static constexpr int TILE_GAP = 10;
    static constexpr int TITLE_LINE = 15;
    static constexpr int LINE_GAP = 1;
    static constexpr int TEXT_LINE = 13;
    static constexpr int STATUS_HEIGHT = TEXT_LINE;  ///< 46 -> 59 (383:9088 file-1)

    explicit PDFAppSplitFileRowDelegate(QObject* parent);

protected:
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
};

/// The tools pages.split and pages.extract (package A of step 3a of phase 2, plan 9.2)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUISplit
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolGUISplit)

public:
    PDFAppToolGUISplit() = delete;

    /// Registers what is not registered yet: the tools in PDFDocumentToolRegistry and their
    /// GUI in PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerSplitTools();

    /// The paths of the documents open in the application (R14: a file of a run never replaces
    /// them), canonical; for PDFAppToolRunEnvironment::protectedFilePaths
    static QStringList getOpenDocumentPaths(const PDFDocumentToolGUI* gui);
};

/// Split (pages.split, Figma 383:3315 ... 403:29978): the pages of the selection of the left
/// panel are separated into files by the rule of the toolbar (Split: Every page, Every N pages
/// with the field "12 pages per file", At selected pages with Clear Cuts, At bookmarks, By file
/// size with the field "10 MB", Odd and even pages; Optimize). The files are computed by the core
/// (PDFDocumentToolSplit::createFiles, the names by PDFDocumentToolFileOutput::createFileNames) -
/// the same as the run.
///
///  - The left panel lists the files to create (383:3315: the number, the name, "Pages 1–12";
///    a click on a row shows its card or its first page); At bookmarks the checklist of the
///    bookmarks "Start a new file at" with Top level stands in the place of the page selection
///    (383:6771). A row says "Same name as another file" (383:9088) and "Written" (383:10232).
///  - The canvas has a card per file (383:3315: the grouped view, its header with the name, the
///    pages and the size of By file size, the Tag "Duplicate name" or "Written"); At selected
///    pages one grid of all pages with the dot of the file of each page, where a click between
///    two pages sets or removes the cut (383:5715).
///  - The right panel: the cuts (383:5715), the callout of By file size (383:7775), Output files
///    with the callout of equal names at the top (383:9088) and the Summary.
///  - While the files are written, the progress card "Writing the files" says "Handbook_3.pdf ·
///    file 3 of 4"; the files written before are marked until the next edit (383:10232,
///    403:29978). The core names the file of each phase of the progress (writeFiles).
///
/// Ownership: the GUI owns the models of the bookmarks; the views and the fields belong to the
/// panels (QPointer); the runner is held by QPointer while its signals are connected.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSplitToolGUI : public PDFAppPageMarkingToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPageMarkingToolGUI;

public:
    explicit PDFAppSplitToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppSplitToolGUI() override;

    /// The files of the values now (empty with an error)
    const std::vector<PDFDocumentToolFileOutput::File>& getFiles() const { return m_files; }
    const QString& getFilesError() const { return m_filesError; }

    /// Index of the file of the selected row of Files to create (its card is shown)
    int getSelectedFile() const { return m_selectedFile; }

    /// Selects the row of the file and shows its card (its first page At selected pages)
    void selectFile(int fileIndex);

    /// Files written by the run now or by the stopped run (their marks Written)
    const std::set<int>& getWrittenFiles() const { return m_writtenFiles; }

    /// Clear Cuts (383:5715): no cut, one undo step
    void clearCuts();

    PDFAppToolOutputFilesSection* getOutputFiles() const { return m_outputFiles; }
    PDFKeyValueList* getSummary() const { return m_summary.data(); }
    PDFAppToolNumberField* getPagesPerFileField() const { return m_pagesPerFile.data(); }
    PDFAppToolNumberField* getSizeField() const { return m_sizeField.data(); }
    QToolButton* getClearCutsButton() const { return m_clearCuts.data(); }
    PDFAppToolCallout* getSizeCallout() const { return m_sizeCallout.data(); }
    QWidget* getBookmarksPart() const { return m_bookmarksPart.data(); }
    QListView* getBookmarkView() const { return m_bookmarkView.data(); }
    PDFAppSplitBookmarkModel* getBookmarkModel() const { return m_bookmarkModel.data(); }
    PDFTextLink* getTopLevelLink() const { return m_topLevelLink.data(); }
    PDFAppParagraph* getBookmarksHint() const { return m_bookmarksHint.data(); }

    /// Toggles the bookmark of the row of the checklist (one undo step)
    void toggleBookmark(int row);

    /// Top level (383:6771): the bookmarks of the top level start the files
    void chooseTopLevel();

    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QString getRunningCardTitle(int percentage, const QString& stepText) const override;
    virtual QString getRunningCardStepText(const QString& stepText) const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void clickPage(int page, Qt::KeyboardModifiers modifiers) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanelParts(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

    virtual QSize getSlotSize() const override;
    virtual QString getSelectionLabel() const override;
    virtual QString getPendingTitle() const override;
    virtual QString getGridHeaderText() const override;
    virtual QString getGridHintText() const override;
    virtual std::vector<PDFInteger> getPendingPages() const override;
    virtual PDFAppToolPendingPagesModel::Row createPendingRow(PDFInteger page) const override;
    virtual PDFAppToolPageGridModel::Tile createTile(PDFInteger page) const override;
    virtual bool isPendingListShown(const std::vector<PDFInteger>& pendingPages) const override;
    virtual void onPendingLinkActivated(PDFInteger page) override;
    virtual void onTargetsUpdated() override;
    virtual void updatePanels() override;
    virtual PDFPageSelection::Mode getEntryModeWithoutSelection() const override;
    virtual PDFAppToolPageGrid::TileSize getDefaultTileSize() const override;
    virtual void onShellStateChanged(pdf::PDFDocumentToolShell::State state) override;

private:
    QString getRule() const;

    /// The template gives one name to every file (383:9088)
    bool isEveryNameEqual() const;

    /// The file has the name of another file (383:9088 "Same name as another file")
    bool isDuplicateName(int fileIndex) const;

    /// The name of the document without its extension ("Handbook")
    QString getSourceBaseName() const;

    /// Index of the file of the name (the file of a phase of the progress), -1 = none
    int findFile(const QString& fileName) const;

    /// "Pages 1–12" / "Introduction · pages 1–6" / "Pages 1–17 · about 9.8 MB" of a file
    QString getFileDescription(const PDFDocumentToolFileOutput::File& file) const;

    /// The rows and the groups of the canvas of the files now (the model announces them: a slot
    /// may delete this GUI - the last call of updatePanels)
    void updateCanvas();

    /// The parts of the rule: the fields of the toolbar, Clear Cuts, Cuts, the callout of the
    /// size, the bookmarks in the place of the page selection (no signal of this GUI)
    void updateRuleParts();

    /// The selected row of Files to create in its view (after the rows are set again)
    void updateFileSelection();

    /// The bookmarks of the document and the checks of the value of the parameter
    void updateBookmarks();

    void onFileRowClicked(const QModelIndex& index);
    void onBookmarkClicked(const QModelIndex& index);
    void onRunProgress(int percentage, const QString& stepText);
    void onRunFinished(const pdf::PDFDocumentToolResult& result);
    void connectRunner();

    std::vector<PDFDocumentToolFileOutput::File> m_files;
    std::vector<int> m_fileOfPage;  ///< Index of the file of each page, -1 = none
    QString m_filesError;
    int m_selectedFile = 0;
    std::set<int> m_writtenFiles;
    PDFDocumentPointer m_bookmarkDocument;  ///< The version, whose bookmarks the checklist shows
    QPointer<PDFDocumentToolRunner> m_runner;
    QPointer<PDFAppToolOutputFilesSection> m_outputFiles;
    QPointer<PDFAppToolNumberField> m_pagesPerFile;
    QPointer<PDFAppToolNumberField> m_sizeField;
    QPointer<QToolButton> m_clearCuts;
    QPointer<QWidget> m_cutsSection;
    QPointer<PDFAppToolCallout> m_sizeCallout;
    QPointer<PDFKeyValueList> m_summary;
    QPointer<QWidget> m_bookmarksPart;
    QPointer<QListView> m_bookmarkView;
    QPointer<PDFAppSplitBookmarkModel> m_bookmarkModel;
    QPointer<PDFTextLink> m_topLevelLink;
    QPointer<PDFAppParagraph> m_bookmarksHint;
};

/// Extract Pages (pages.extract, Figma 385:9405 ... 403:31676): the pages of the selection are
/// copied, in the order of the document (K49), as a new document (Output: One file, R2) or as a
/// file per page (One file per page, R3 with Output files). The source never changes - a locked
/// document may be extracted (4.7).
///
/// The canvas shows only the pages of the result (385:9405: "1 · from page 8"; 385:11029: the
/// name of the file · "page 8"), nothing is marked red (red is a removal); the left panel has the
/// thumbnails of the source below the page selection (as Crop): the selected thumbnails are the
/// pages to extract, a click on one adds or removes it. Delete on a page of the result leaves it
/// out.
///
/// Ownership: the thumbnail view belongs to the left panel, the GUI holds it, its model, delegate
/// and scheduler (children of the view) by QPointer.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppExtractToolGUI : public PDFAppPageMarkingToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPageMarkingToolGUI;

public:
    /// The slot of the thumbnails of the source (385:9405 thumbnail 124 x 183: the page 108 x 148)
    static constexpr QSize THUMBNAIL_SLOT = QSize(108, 148);

    explicit PDFAppExtractToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppExtractToolGUI() override;

    /// One file per page is chosen
    bool isFilePerPage() const;

    PDFAppToolOutputFilesSection* getOutputFiles() const { return m_outputFiles; }
    PDFKeyValueList* getSummary() const { return m_summary.data(); }
    PDFAppThumbnailListView* getThumbnailView() const { return m_thumbnailView.data(); }

    /// The button Output of the toolbar; its icon is the icon of the chosen output (385:9405
    /// icon/file "One file", 385:11029 icon/split-pages "One file per page")
    QToolButton* getOutputButton() const { return m_outputButton.data(); }
    QString getOutputIconName() const;

    /// A click on the page of the canvas: the pages are chosen in the thumbnails (385:9405)
    virtual void clickPage(int page, Qt::KeyboardModifiers modifiers) override;

    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void onDocumentChanged() override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanelParts(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

    virtual QSize getSlotSize() const override;
    virtual QString getSelectionLabel() const override;
    virtual QString getPendingTitle() const override;
    virtual QString getGridHeaderText() const override;
    virtual QString getGridHintText() const override;
    virtual std::vector<PDFInteger> getPendingPages() const override;
    virtual PDFAppToolPendingPagesModel::Row createPendingRow(PDFInteger page) const override;
    virtual PDFAppToolPageGridModel::Tile createTile(PDFInteger page) const override;
    virtual bool isPendingListShown(const std::vector<PDFInteger>& pendingPages) const override;
    virtual void onPendingLinkActivated(PDFInteger page) override;
    virtual void onDeleteRequested(PDFInteger page) override;
    virtual void onTargetsUpdated() override;
    virtual void updatePanels() override;
    virtual PDFAppToolPageGrid::TileSize getDefaultTileSize() const override;

private:
    void updateThumbnails();
    void onThumbnailClicked(const QModelIndex& index);
    void clickThumbnail(int page, Qt::KeyboardModifiers modifiers);

    QPointer<PDFAppToolOutputFilesSection> m_outputFiles;
    QPointer<QToolButton> m_outputButton;
    QPointer<PDFKeyValueList> m_summary;
    QPointer<PDFAppThumbnailListView> m_thumbnailView;
    QPointer<PDFAppThumbnailsModel> m_thumbnailModel;
    QPointer<PDFAppThumbnailScheduler> m_thumbnailScheduler;
    std::vector<PDFDocumentToolFileOutput::File> m_pageFiles;   ///< One file per page: a file of each page of the result with its name
    QString m_namesError;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_SPLIT_H
