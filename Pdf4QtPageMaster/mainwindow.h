// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFPAGEMASTER_MAINWINDOW_H
#define PDFPAGEMASTER_MAINWINDOW_H

#include "pdficontheme.h"
#include "pdfpagegeometry.h"

#include "pageitemmodel.h"
#include "pageitemdelegate.h"

#include <QMainWindow>
#include <QJsonObject>
#include <QList>
#include <QPoint>
#include <QSignalMapper>
#include <QStringList>
#include <QUrl>

namespace Ui
{
class MainWindow;
}

class QAction;
class QAbstractItemView;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QMenu;
class QMimeData;
class QTableView;

namespace pdfpagemaster
{

class PageItemPreviewRenderer;
class WorkspaceFilterProxyModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent);
    virtual ~MainWindow() override;

    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;

    QSize getMinPageImageSize() const;
    QSize getDefaultPageImageSize() const;
    QSize getMaxPageImageSize() const;

    enum class Operation
    {
        Clear,
        CloneSelection,
        RemoveSelection,
        ReplaceSelection,
        RestoreRemovedItems,

        Undo,
        Redo,

        Cut,
        Copy,
        Paste,

        RotateLeft,
        RotateRight,
        ResetRotation,

        Group,
        Ungroup,
        RenameGroup,
        Properties,
        CropPages,

        SelectNone,
        SelectAll,
        SelectEven,
        SelectOdd,
        SelectPortrait,
        SelectLandscape,
        InvertSelection,
        SelectPageRange,

        ZoomIn,
        ZoomOut,

        Unite,
        Separate,
        SeparateGrouped,
        Split,

        InsertImage,
        InsertEmptyPage,
        InsertPDF,
        ConfigurePageGeometry,
        InsertPDFPages,

        RegroupEvenOdd,
        RegroupPaired,
        RegroupOutline,
        RegroupAlternatingPages,
        RegroupAlternatingPagesReversed,
        RegroupReversed,

        GetSource,
        BecomeSponsor,
        About,
        PrepareIconTheme,
        ShowDocumentTitle,
        ShowDetailsView,
        SortByFileName,
        SortBySource,
        SortByPageNumber,
        SortByType,
        ReverseOrder,
        SelectVisible,
        SaveWorkspace,
        OpenWorkspace,
        SaveCheckpoint,
        LoadCheckpoint,
    };

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void resizeEvent(QResizeEvent* resizeEvent) override;

private slots:
    void on_actionClose_triggered();
    void on_actionAddDocuments_triggered();
    void onMappedActionTriggered(int actionId);
    void onPreviewUpdated();
    void onWorkspaceCustomContextMenuRequested(const QPoint& point);
    void updateActions();
    void onClearRecentTriggered();
    void onRecentSourceFileTriggered();
    void onRecentWorkspaceFileTriggered();
    void onRecentDirectoryTriggered();

private:
    void loadSettings();
    void saveSettings();
    bool insertDocument(const QString& fileName, const QModelIndex& insertIndex);
    bool insertDocument(const QString& fileName, const QModelIndex& insertIndex, const std::vector<pdf::PDFInteger>& pages);

    bool canPerformOperation(Operation operation) const;
    void performOperation(Operation operation);
    QModelIndexList getSelectedRows() const;
    QModelIndexList getSelectedRowsOrAll() const;
    QModelIndexList getSelectedRowsForOrdering() const;
    QModelIndexList getVisibleRows() const;
    void exportAssembledDocuments(std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> assembledDocuments, const QString& assembleModeText, std::vector<QString> assembledDocumentGroupNames = {});
    void splitDocuments();
    void selectPageRange();
    void selectSourceSelection(const QItemSelection& selection, bool addToExisting = false);
    void setDetailsViewVisible(bool visible);
    void showItemProperties();
    void cropPages();
    void saveWorkspace();
    void openWorkspace();
    void saveCheckpoint();
    void loadCheckpoint();
    bool openWorkspaceFile(const QString& fileName);
    void addRecentSourceFile(const QString& fileName);
    void addRecentWorkspaceFile(const QString& fileName);
    void addRecentDirectory(const QString& directory);
    void updateRecentMenu();
    void addRecentMenuSection(const QString& title, const QStringList& paths, void (MainWindow::*slot)(), bool* hasItems);
    QJsonObject createWorkspaceStateJson() const;
    bool restoreWorkspaceStateFromJson(const QJsonObject& state, QString* errorMessage);
    QJsonObject createProjectJson() const;
    bool loadProjectJson(const QJsonObject& project, QString* errorMessage);
    void updateSearchFilter();
    void updateSearchResultLabel();
    bool isWorkspaceExternalDrop(const QMimeData* mimeData) const;
    bool isSupportedWorkspaceDropUrl(const QUrl& url) const;
    QList<QUrl> getSupportedWorkspaceDropUrls(const QMimeData* mimeData, int* unsupportedCount) const;
    QAbstractItemView* getWorkspaceDropView(QObject* watched) const;
    int getWorkspaceDropInsertProxyRow(QAbstractItemView* view, const QPoint& viewportPosition) const;
    int getWorkspaceDropInsertSourceRow(QAbstractItemView* view, const QPoint& viewportPosition) const;
    void updateWorkspaceDropFeedback(QAbstractItemView* view, const QPoint& viewportPosition, int insertProxyRow, const QString& message, bool accepted);
    void hideWorkspaceDropFeedback();
    bool dropWorkspaceExternalMimeData(const QMimeData* mimeData, int insertSourceRow);
    bool insertDocument(const QString& fileName, int insertRow, const std::vector<pdf::PDFInteger>& pages = {});

    struct Settings
    {
        QString directory;
        QStringList recentSourceFiles;
        QStringList recentWorkspaceFiles;
        QStringList recentDirectories;
    };

    Ui::MainWindow* ui;

    pdf::PDFIconTheme m_iconTheme;
    PageItemModel* m_model;
    PageItemPreviewRenderer* m_previewRenderer;
    PageItemDelegate* m_delegate;
    WorkspaceFilterProxyModel* m_filterModel;
    QTableView* m_detailsView;
    QAction* m_sortByFileNameAction;
    QAction* m_sortBySourceAction;
    QAction* m_sortByPageNumberAction;
    QAction* m_sortByTypeAction;
    QAction* m_reverseOrderAction;
    QAction* m_resetRotationAction;
    QAction* m_renameGroupAction;
    QAction* m_propertiesAction;
    QAction* m_cropPagesAction;
    QAction* m_showDetailsViewAction;
    QAction* m_insertPDFPagesAction;
    QAction* m_splitAction;
    QAction* m_selectPageRangeAction;
    QAction* m_saveWorkspaceAction;
    QAction* m_openWorkspaceAction;
    QAction* m_saveCheckpointAction;
    QAction* m_loadCheckpointAction;
    QMenu* m_recentMenu;
    QAction* m_clearRecentAction;
    QAction* m_clearSearchAction;
    QAction* m_selectVisibleAction;
    QLineEdit* m_searchEdit;
    QLabel* m_searchResultLabel;
    QLabel* m_dropFeedbackLabel;
    QFrame* m_dropInsertionMarker;
    QWidget* m_dropFeedbackViewport;
    Settings m_settings;
    QSignalMapper m_mapper;
    Qt::DropAction m_dropAction;
    bool m_hasPageGeometrySettings = false;
    pdf::PDFPageGeometrySettings m_pageGeometrySettings;

    struct WorkspaceCheckpoint
    {
        QString name;
        QJsonObject state;
    };

    std::vector<WorkspaceCheckpoint> m_checkpoints;
};

}   // namespace pdfpagemaster

#endif // PDFPAGEMASTER_MAINWINDOW_H
