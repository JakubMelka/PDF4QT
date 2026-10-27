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

#include <map>
#include <set>
#include <vector>

#include <QSlider>
#include <QToolButton>

class QMenu;
class QTimer;

namespace pdf
{

class PDFAppThumbnailsModel;
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

/// Scheduler of the pictures of the thumbnails (newapp/step3_large_documents.md):
/// as the Editor, a picture is drawn from the compiled page of the canvas in the
/// GUI thread (PDFDrawWidgetProxy::drawThumbnailImage: the annotations, the color
/// mode, the layers and the color management of the view). A page, which is not
/// compiled, is requested from the compiler of the view; such requests cannot be
/// canceled, so at most MAXIMUM_PENDING_COMPILES of them are waiting at once and
/// only the rows visible in the view are requested - scrolling over 2000 pages
/// queues nothing more. A request, which does not end (a compiler stopped by a
/// change of the document), expires after PENDING_TIMEOUT_MS.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppThumbnailScheduler : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Most compilations of the pages of the thumbnails waiting at once
    static constexpr int MAXIMUM_PENDING_COMPILES = 4;

    /// Most pictures drawn in one pass of the event loop
    static constexpr int MAXIMUM_PICTURES_PER_PASS = 8;

    /// Time after which a request of a compilation is forgotten
    static constexpr int PENDING_TIMEOUT_MS = 10000;

    /// Interval of the next try of the visible pages, which wait for a compilation
    static constexpr int RETRY_INTERVAL_MS = 500;

    explicit PDFAppThumbnailScheduler(PDFAppThumbnailsModel* model, QAbstractItemView* view, QObject* parent);
    virtual ~PDFAppThumbnailScheduler() override;

    /// Proxy of the canvas, whose compiled pages are drawn (nullptr = none)
    void setProxy(PDFDrawWidgetProxy* proxy);

    /// Forgets the requests (a new document)
    void reset();

    /// Number of the waiting compilations (tests)
    int getPendingCount() const { return int(m_pending.size()); }

    /// Largest number of waiting compilations seen (tests)
    int getMaximumPendingCount() const { return m_maximumPending; }

    /// Number of the drawn pictures (tests)
    int getDrawnCount() const { return m_drawnCount; }

    /// Draws the pictures of the visible rows now (also from a timer)
    void process();

private:
    void onPictureRequested();
    void onPageImageChanged(bool all, const std::vector<PDFInteger>& pages);
    void onProxyDestroyed();
    bool isRowVisible(int row) const;
    void schedule();

    QPointer<PDFAppThumbnailsModel> m_model;
    QPointer<QAbstractItemView> m_view;
    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<QTimer> m_timer;
    QPointer<QTimer> m_retryTimer;
    std::set<int> m_waiting;                            ///< Visible pages without a picture
    std::map<PDFInteger, QElapsedTimer> m_pending;      ///< Requested compilations
    int m_maximumPending = 0;
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
    QSize getItemSize() const { return m_itemSize; }

Q_SIGNALS:
    /// The context menu of the selection is requested at the global position
    void contextMenuRequested(const QPoint& globalPosition);

protected:
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;
    virtual void updateGeometries() override;

private:
    void updateMargins();

    QSize m_itemSize;
    bool m_isUpdatingMargins = false;
};

/// Thumbnails panel (sidebar of pdf4qt-document 193:756, selection 266:12502):
/// the header "Thumbnails / 1,592 pages" ("3 of 1,592 pages selected") with the
/// size slider (thumbnail-size 193:921, 84 px) and the sync toggle
/// (thumbnail-sync 193:925, icon locate: the thumbnails follow the current page),
/// the grid of PDFThumbnailDelegate. A click goes to the page, Ctrl / Shift select
/// more pages (PDFAppDocumentView::getSelectedPages). The context menu 266:12848:
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

    /// Creates the context menu of the selection (deleted, when it is closed)
    QMenu* createContextMenu();

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
    void onInsertPages();
    void onMovePages();
    void onExtractPages();
    void onRemovePages();
    void applyDocument();
    void updateViewSelection();
    void updatePictureSize();
    void syncToCurrentPage(bool force);
    void openTool(const QString& toolId);
    void executeCommand(const QString& commandId);

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
    PDFInteger m_pendingPageIndex = -1;
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
