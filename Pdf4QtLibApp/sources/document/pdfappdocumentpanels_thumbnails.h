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

#ifndef PDFAPPDOCUMENTPANELS_THUMBNAILS_H
#define PDFAPPDOCUMENTPANELS_THUMBNAILS_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanel.h"

#include "pdfcomponentdelegates.h"
#include "pdffluentwidgets.h"
#include "pdfdocument.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QPointer>
#include <QSize>

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <QSlider>
#include <QToolButton>

class QMenu;
class QTimer;

namespace pdf
{

class PDFAppDocument;
class PDFAppThumbnailsModel;
class PDFAppThumbnailRenderJob;
class PDFAppThumbnailRenderLink;
class PDFDrawWidgetProxy;

/// Pages of the thumbnails (one row per page). The picture of a page (PixmapRole)
/// is asked only for the painted rows (as the Editor, newapp/step3_large_documents.md):
/// a page, whose picture is not in QPixmapCache, is requested from the scheduler
/// of the panel and painted as the placeholder of the Thumbnail item until the
/// picture is ready. The keys of QPixmapCache contain the id of the document, a
/// generation unique in the process (a new one for every change of the document
/// and of the drawing), the page, its own generation (a recompiled page), the
/// size and the device pixel ratio, so the thumbnails of two tabs never share a
/// picture and a changed page is never shown from an old picture.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppThumbnailsModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    explicit PDFAppThumbnailsModel(QObject* parent);
    virtual ~PDFAppThumbnailsModel() override;

    /// Sets the document; the rows are reset, when the number of pages changes
    void setDocument(PDFDocumentPointer document, const QString& documentId);

    /// The document of the pages (nullptr = none)
    const PDFDocumentPointer& getDocument() const { return m_document; }

    /// Size of the picture of a page in logical pixels (the slot of the delegate)
    /// and the device pixel ratio of the view
    void setPictureSize(const QSize& size, qreal devicePixelRatio);
    QSize getPictureSize() const { return m_pictureSize; }

    /// All pictures are drawn again (the drawing or the document has changed)
    void invalidateAll();

    /// The pictures of the pages are drawn again (a page has been compiled again)
    void invalidatePages(const std::vector<PDFInteger>& pages);

    /// Key of the picture of the page in QPixmapCache
    QString getKey(int pageIndex) const;

    /// The picture of the page is ready (in QPixmapCache)
    bool hasPicture(int pageIndex) const;

    /// Stores the picture of the page and repaints its row
    void setPicture(int pageIndex, const QPixmap& pixmap);

    /// Pages, whose pictures were asked by the painting since the last call
    std::vector<int> takeRequestedPages();

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

Q_SIGNALS:
    /// A painted row has no picture (the scheduler is asked)
    void pictureRequested();

private:
    PDFDocumentPointer m_document;
    QString m_documentId;
    int m_pageCount = 0;
    quint64 m_generation = 0;
    std::vector<quint32> m_pageGenerations;
    QSize m_pictureSize = QSize(108, 148);
    qreal m_devicePixelRatio = 1.0;
    mutable std::set<int> m_requestedPages;
};

/// Scheduler of the pictures of the thumbnails (newapp/step3_large_documents.md) of the
/// panel Thumbnails and of the page grid of the tools. Only the rows visible in the view
/// ask for their pictures (scrolling over 2000 pages queues nothing more); the pages are
/// rendered in parallel in the thread pool (PDFExecutionPolicy::startTask, at most
/// getRenderThreadCount() pages at once), each worker with its own PDFAppPageRenderContext
/// - its own compilation, never a compiled page of the canvas, so no QPainterPath is used by
/// two threads. The pictures have what the view shows: the features (color mode,
/// annotations), the color management and the states of the layers, taken as values in the
/// GUI thread when a render job starts.
///
/// Ownership and lifetime: the scheduler owns its current render job by a shared pointer;
/// the job holds only plain data (the document pointer, the settings, the queue and the
/// finished images) and a link back to the scheduler, which the destructor cuts. The
/// workers hold the job, never the scheduler; nobody waits for them: a new document, other
/// settings, the deletion of the view cancel the job (the compilation stops at its next
/// check) and a late result is dropped. A page, which has scrolled away before a worker
/// took it, leaves the queue. The images come back by a queued call to the GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppThumbnailScheduler : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Most pages queued or being rendered at once (more visible rows wait for the next pass)
    static constexpr int MAXIMUM_PENDING_PAGES = 64;

    /// Most pages rendered at once (the threads of the pool used by one view)
    static constexpr int MAXIMUM_RENDER_THREADS = 16;

    /// A worker without work waits so long for the next page before it ends (its caches
    /// stay for the next scroll); meanwhile its thread belongs to the other tasks of the pool
    static constexpr int WORKER_IDLE_TIMEOUT_MS = 1000;

    explicit PDFAppThumbnailScheduler(PDFAppThumbnailsModel* model, QAbstractItemView* view, QObject* parent);
    virtual ~PDFAppThumbnailScheduler() override;

    /// Proxy of the canvas, whose settings the pictures follow (nullptr = the defaults)
    void setProxy(PDFDrawWidgetProxy* proxy);

    /// The document of the application, whose states of the layers the pictures show (its
    /// own optional content activity, only while its document is the one of the model;
    /// nullptr = the default states)
    void setAppDocument(PDFAppDocument* document);

    /// Forgets the requests and cancels the rendering (a new document)
    void reset();

    /// Number of the pages queued or being rendered (tests)
    int getPendingCount() const { return int(m_requested.size()); }

    /// Largest number of pages queued or being rendered seen (tests)
    int getMaximumPendingCount() const { return m_maximumPending; }

    /// Number of the drawn pictures (tests)
    int getDrawnCount() const { return m_drawnCount; }

    /// Largest number of the pages rendered at once by the workers (tests)
    int getMaximumRenderingCount() const { return m_maximumRenderingCount; }

    /// Number of the pages rendered at once (PDFExecutionPolicy, at most MAXIMUM_RENDER_THREADS)
    static int getRenderThreadCount();

    /// Queues the pictures of the visible rows now (also from a timer)
    void process();

private:
    friend class PDFAppThumbnailRenderLink;

    void onPictureRequested();
    void onPageImageChanged(bool all, const std::vector<PDFInteger>& pages);
    void onProxyDestroyed();
    void onRenderResults(const std::shared_ptr<pdf::PDFAppThumbnailRenderJob>& job);
    bool isRowVisible(int row) const;
    void schedule();
    void cancelJob();
    void startJob();

    QPointer<PDFAppThumbnailsModel> m_model;
    QPointer<QAbstractItemView> m_view;
    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<PDFAppDocument> m_appDocument;
    QPointer<QTimer> m_timer;
    std::shared_ptr<PDFAppThumbnailRenderLink> m_link;
    std::shared_ptr<PDFAppThumbnailRenderJob> m_job;
    std::set<int> m_waiting;                            ///< Visible pages without a picture
    /// A page queued or being rendered by the job: the key of its picture and its own
    /// cancellation (set, when its row scrolls away; read by the worker)
    struct Request
    {
        QString key;
        std::shared_ptr<std::atomic_bool> isCanceled;
    };

    std::map<int, Request> m_requested;                 ///< Pages queued or being rendered by the job
    int m_maximumPending = 0;
    int m_maximumRenderingCount = 0;
    int m_drawnCount = 0;
};

/// List of the thumbnails (pdf4qt-document 193:756): a grid of the Thumbnail
/// items with the gaps 8 / 6, the columns centered in the sidebar
class PDF4QTLIBAPPSHARED_EXPORT PDFAppThumbnailListView : public PDFListView
{
    Q_OBJECT

private:
    using BaseClass = PDFListView;

public:
    explicit PDFAppThumbnailListView(QWidget* parent);
    virtual ~PDFAppThumbnailListView() override;

    /// Size of one item (the grid adds the gaps)
    void setItemSize(const QSize& size);

    /// Least space at each side of the columns (the padding of the sidebar, 14 by default); a
    /// panel, which has its own padding, gives 0 (the left panel of Extract Text, 339:8282)
    void setSidePadding(int padding);
    QSize getItemSize() const { return m_itemSize; }

    /// A press or a release of the right button is handled: the page under it is
    /// selected for the context menu, the canvas does not go to it
    bool isContextClick() const { return m_isContextClick; }

Q_SIGNALS:
    /// The context menu of the selection is requested at the global position
    void contextMenuRequested(const QPoint& globalPosition);

protected:
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void updateGeometries() override;

private:
    void updateMargins();

    QSize m_itemSize;
    int m_sidePadding = -1;     ///< -1 = the padding of the sidebar
    bool m_isUpdatingMargins = false;
    bool m_isContextClick = false;
};

/// Thumbnails panel (sidebar of pdf4qt-document 193:756, selection 266:12502):
/// the header "Thumbnails / 1,592 pages" ("3 of 1,592 pages selected") with the
/// size slider (thumbnail-size 193:921, 84 px) and the sync toggle
/// (thumbnail-sync 193:925, icon locate: the thumbnails follow the current page),
/// the grid of PDFThumbnailDelegate. A click goes to the page, Ctrl / Shift select
/// more pages (PDFAppDocumentView::getSelectedPages). The sync never moves the one
/// page chosen by the user (a click, the keys, the right button), while it is seen in
/// the canvas (the right page of a row of two pages, a page below the current page):
/// the selection follows the canvas, when its page has left the canvas; the view shows
/// the chosen page as the page for the user (PDFAppDocumentView::getDisplayedPageIndex).
/// A page selected by the sync follows every change of the current page. The context
/// menu 266:12848:
/// Rotate Left / Right (the commands pages.rotateLeft / pages.rotateRight - one
/// undo step), Insert Pages…, Move to…, Extract to New Document, Remove Pages
/// (routes of the tools /document/{id}/tool/{toolId}, Page Not Found in phase 1).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppThumbnailsPanel : public PDFAppDocumentPanel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanel;

public:
    /// Slider of the size: the width of the slot of the page (the height keeps 108 : 148)
    static constexpr int MINIMUM_SLOT_WIDTH = 60;
    static constexpr int MAXIMUM_SLOT_WIDTH = 156;
    static constexpr int DEFAULT_SLOT_WIDTH = 108;

    explicit PDFAppThumbnailsPanel(QWidget* parent);
    virtual ~PDFAppThumbnailsPanel() override;

    virtual QString getId() const override;
    virtual QString getTitle() const override;
    virtual QString getSubtitle() const override;
    virtual QString getIconName() const override;
    virtual void setDocument(PDFAppDocument* document, PDFAppDocumentView* view) override;
    virtual QWidget* createHeaderWidget(QWidget* parent) override;

    /// Selected pages, sorted (page indices from 0)
    std::vector<PDFInteger> getSelectedPages() const;

    /// Selects the pages (invalid pages are ignored)
    void selectPages(const std::vector<PDFInteger>& pages);

    /// The thumbnails follow the current page of the canvas
    bool isSyncEnabled() const { return m_isSyncEnabled; }
    void setSyncEnabled(bool enabled);

    /// Width of the slot of the page (MINIMUM_SLOT_WIDTH - MAXIMUM_SLOT_WIDTH)
    int getSlotWidth() const { return m_slotWidth; }
    void setSlotWidth(int width);

    /// Creates the context menu of the selection (deleted, when it is closed); its
    /// commands act on the pages selected now (getContextPages)
    QMenu* createContextMenu();

    /// Pages of the last context menu (indices of the version, for which it was opened)
    const std::vector<PDFInteger>& getContextPages() const { return m_contextPages; }

    /// Pages of the last context menu in the current version of the document: a page
    /// is found by its reference (a new version may have inserted or removed pages
    /// before it while the menu was open), a removed page is left out
    std::vector<PDFInteger> getCurrentContextPages() const;

    PDFAppThumbnailListView* getListView() const { return m_view; }
    PDFAppThumbnailsModel* getModel() const { return m_model; }
    PDFAppThumbnailScheduler* getScheduler() const { return m_scheduler; }
    QSlider* getSizeSlider() const { return m_sizeSlider; }
    QToolButton* getSyncButton() const { return m_syncButton; }

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void onDocumentChanged(const PDFModifiedDocument& modified);
    void onDocumentDestroyed();
    void onConnectedViewDestroyed();
    void onCurrentPageChanged(PDFInteger pageIndex);
    void onCanvasViewChanged();
    void updateShownSelectedPage();
    /// The one selected page (-1 for no page or more pages), cheap for a range of pages
    PDFInteger getSingleSelectedPage() const;
    void onViewClicked(const QModelIndex& index);
    void onViewActivated(const QModelIndex& index);
    void scheduleGoToPage(PDFInteger pageIndex);
    void onGoToPageQueued();
    void onSelectionChanged();
    void onSelectionChangeQueued();
    bool processSelectionChange();
    void onContextMenuRequested(const QPoint& globalPosition);
    void onSizeSliderChanged(int value);
    void onSyncToggled(bool checked);
    void onRotateLeft();
    void onRotateRight();
    void rotateContextPages(bool isRight);
    void onInsertPages();
    void onMovePages();
    void onExtractPages();
    void onRemovePages();
    /// \param initialSelection The selection of a view, which the panel joins (a new version
    ///        keeps the selection of the panel instead)
    void applyDocument(bool isNewVersion, const std::vector<PDFInteger>& initialSelection = std::vector<PDFInteger>());
    void updateViewSelection();
    void updatePictureSize();
    void syncToCurrentPage(bool force);
    void openTool(const QString& toolId);

    QPointer<PDFAppThumbnailListView> m_view;
    QPointer<PDFAppThumbnailsModel> m_model;
    QPointer<PDFThumbnailDelegate> m_delegate;
    QPointer<PDFAppThumbnailScheduler> m_scheduler;
    QPointer<QSlider> m_sizeSlider;
    QPointer<QToolButton> m_syncButton;
    QPointer<PDFAppDocument> m_connectedDocument;
    QPointer<PDFAppDocumentView> m_connectedView;
    bool m_isSyncEnabled = true;
    bool m_isSyncing = false;
    bool m_isSelectionChangePending = false;
    bool m_isUserSelectionPending = false;
    PDFInteger m_userSelectedPage = -1;      ///< The one page chosen by the user (-1: none, more pages, a page of the sync)
    PDFInteger m_pendingPageIndex = -1;
    PDFInteger m_lastCurrentPageIndex = -1;  ///< Current page of the canvas last seen (the selection follows its changes)
    PDFInteger m_shownSelectedPage = -1;     ///< The page chosen by the user, when it was seen in the canvas at the last check (-1 otherwise)
    std::vector<PDFInteger> m_contextPages;
    std::vector<PDFObjectReference> m_contextPageReferences;  ///< The pages of m_contextPages (their version may be older)
    int m_slotWidth = DEFAULT_SLOT_WIDTH;
};

/// Factory of the thumbnails panel
class PDF4QTLIBAPPSHARED_EXPORT PDFAppThumbnailsPanelFactory : public PDFAppDocumentPanelFactory
{
public:
    virtual QString getId() const override;
    virtual int getOrder() const override;
    virtual QString getTitle() const override;
    virtual QString getIconName() const override;
    virtual PDFAppDocumentPanel* createPanel(QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPANELS_THUMBNAILS_H
