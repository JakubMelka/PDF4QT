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

#ifndef PDFCREATEBITONALDOCUMENTDIALOG_H
#define PDFCREATEBITONALDOCUMENTDIALOG_H

#include "pdfbitonaldocumentcreator.h"
#include "pdfcms.h"
#include "pdfdocument.h"
#include "pdfimageconversion.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"

#include <QDialog>
#include <QFuture>
#include <QFutureWatcher>
#include <QStyledItemDelegate>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace Ui
{
class PDFCreateBitonalDocumentDialog;
}

namespace pdf
{
class PDFPage;
class PDFRasterizerPool;
class PDFDrawWidgetProxy;
class PDFOptionalContentActivity;
}

namespace pdfviewer
{

/// Cancellation token of a background job. It is shared between the GUI thread,
/// which cancels the job, and the worker thread, which polls it. A cancelled job
/// is not joined immediately (rasterizing of a page cannot be interrupted in the
/// middle), so the token must outlive the job it has been created for - that is
/// why it is always held by a shared pointer.
class PDFOperationCancelToken : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return m_cancelled.load(std::memory_order_acquire); }

    /// Cancels the operation. It can be called from any thread.
    void cancel() { m_cancelled.store(true, std::memory_order_release); }

private:
    std::atomic<bool> m_cancelled = { false };
};

class PDFCreateBitonalDocumentPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    PDFCreateBitonalDocumentPreviewWidget(QWidget* parent);
    virtual ~PDFCreateBitonalDocumentPreviewWidget() override;

    virtual void paintEvent(QPaintEvent* event) override;

    void setCaption(QString caption);
    void setImage(QImage image);

    /// Turns on the indication, that the displayed image is being generated in the
    /// background. The indication is displayed only until a non-null image is set.
    void setGenerating(bool generating);

private:
    QString m_caption;
    QImage m_image;
    bool m_generating = false;
};

/// Dialog creating a bitonal version of the document. The conversion itself is
/// performed by \p pdf::PDFBitonalDocumentCreator - this dialog is responsible for
/// the settings, for the list of the converted items and for the preview, all of
/// which are generated in the background, so the dialog stays responsive.
class PDFCreateBitonalDocumentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFCreateBitonalDocumentDialog(const pdf::PDFDocument* document,
                                            pdf::PDFDrawWidgetProxy* proxy,
                                            const pdf::PDFCMS* cms,
                                            pdf::PDFProgress* progress,
                                            QWidget* parent);
    virtual ~PDFCreateBitonalDocumentDialog() override;

    pdf::PDFDocument takeBitonaldDocument() { return qMove(m_bitonalDocument); }

    using ConversionSource = pdf::PDFBitonalDocumentCreator::ConversionSource;
    using ConversionSettings = pdf::PDFBitonalDocumentCreator::Settings;

    /// Item of the list, i.e. an image or a page. Besides the information needed by
    /// the conversion it carries the state of the thumbnail, which is generated in
    /// the background - all items are created at once, so the user sees the whole
    /// list immediately, but their thumbnails arrive one by one.
    struct ConversionItemInfo : public pdf::PDFBitonalDocumentCreator::ItemInfo
    {
        using Mode = pdf::PDFBitonalDocumentCreator::ItemMode;

        enum class ThumbnailState
        {
            Pending,    ///< Thumbnail is being generated
            Ready,      ///< Thumbnail has been generated
            Failed      ///< Image of the item cannot be decoded or rendered
        };

        /// Returns true, if the mode can be used for this item. An item, whose image
        /// could not be decoded or rendered, cannot be converted by the algorithm. A
        /// page can still be replaced by a solid fill, because the fill of a page needs
        /// no rasterization. An image cannot - its transparency, which the fill has to
        /// preserve, is a part of the image, which cannot be decoded - so the converter
        /// would skip it and the request of the user would be silently ignored.
        /// \param testedMode Mode to be tested
        bool isModeAvailable(Mode testedMode) const
        {
            if (thumbnailState != ThumbnailState::Failed || testedMode == Mode::Original)
            {
                return true;
            }

            return testedMode != Mode::Algorithm && pageIndex >= 0;
        }

        ThumbnailState thumbnailState = ThumbnailState::Pending;
    };

protected:
    virtual void done(int r) override;

signals:
    /// Emitted by a worker thread, when a thumbnail of an item is generated. The
    /// connection is a queued one, so the thumbnail is delivered into the GUI thread.
    /// \param generation Generation of the thumbnail job, which created the thumbnail
    /// \param itemIndex Index of the item in the list
    /// \param thumbnail Thumbnail image (a null image means a failure)
    void thumbnailReady(int generation, int itemIndex, QImage thumbnail);

    /// Emitted by a worker thread, when the preview images are generated. The
    /// connection is a queued one, so they are delivered into the GUI thread.
    /// \param generation Generation of the preview job, which created the images
    /// \param originalImage Decoded image or rasterized page
    /// \param bitonalImage Result of the conversion of the original image
    void previewReady(int generation, QImage originalImage, QImage bitonalImage);

private:
    /// Background job of the dialog. Only the result of the newest run of the job is
    /// interesting - when a new run is started, the previous one is cancelled and its
    /// results are dropped. A cancelled worker is not joined at that moment, because
    /// it can be in the middle of an operation, which cannot be interrupted, so the
    /// futures of all unfinished runs are kept and joined before the dialog dies.
    struct AsyncJob
    {
        /// Number of the newest run. Every result is tagged with the generation of the
        /// run, which produced it, so the results of the superseded runs are recognized
        /// and thrown away.
        int generation = 0;

        /// True, when the newest run has not finished yet
        bool isRunning = false;

        /// Cancellation token of the newest run
        std::shared_ptr<PDFOperationCancelToken> cancelToken;

        /// Futures of all runs, which have not finished yet
        std::vector<QFuture<void>> futures;

        /// Watcher of the newest run
        std::optional<QFutureWatcher<void>> futureWatcher;
    };

    /// Immutable snapshot of the inputs of the thumbnail generation
    struct ThumbnailRequest
    {
        ConversionSource conversionSource = ConversionSource::Images;
        QSize thumbnailSize;
        std::vector<pdf::PDFObjectReference> imageReferences;  ///< Valid, when images are converted
        std::vector<pdf::PDFInteger> pageIndices;              ///< Valid, when pages are converted
    };

    /// Immutable snapshot of the inputs of the preview
    struct PreviewRequest
    {
        ConversionSource conversionSource = ConversionSource::Images;
        ConversionItemInfo item;
        pdf::PDFImageConversion::ConversionMethod conversionMethod = pdf::PDFImageConversion::ConversionMethod::Automatic;
        int manualThreshold = 128;
        int dpiResolution = pdf::PDFBitonalDocumentCreator::DEFAULT_DPI_RESOLUTION;

        /// Rasterized page, when it has already been rendered for the previous preview.
        /// It is passed to the worker, so changing the conversion method does not
        /// rasterize the same page again.
        QImage cachedPageImage;
    };

    /// Starts a new run of the job in a worker thread. The previous run is cancelled
    /// first. The worker function is called with the generation of its run and with
    /// the cancellation token of the run, which it must poll.
    /// \param job Job to be started
    /// \param worker Worker function executed in the worker thread
    void startJob(AsyncJob& job, std::function<void(int, const pdf::PDFOperationControl*)> worker);

    /// Cancels the currently running instance of the job. The worker is not joined,
    /// it can still be finishing an operation, which cannot be interrupted. Results
    /// of the cancelled run are dropped, because the generation of the job is
    /// increased and they do not match it anymore.
    /// \param job Job to be cancelled
    void cancelJob(AsyncJob& job);

    /// Cancels the job and waits until all its workers have finished. It must be
    /// called before anything, which the workers use, is destroyed.
    /// \param job Job to be finished
    void finishJob(AsyncJob& job);

    /// Handles the end of a run of the job. Runs, which have been superseded, are
    /// ignored - the job is running until its newest run finishes.
    void onJobFinished(AsyncJob& job, int generation);

    /// Result of the conversion. The counts belong to the same run as the flag, so
    /// the dialog can tell a complete success from a partial one.
    struct ConversionResult
    {
        bool isConverted = false;       ///< True, if at least one item has been converted
        size_t convertedItemCount = 0;  ///< Number of the converted items
        size_t failedItemCount = 0;     ///< Number of the items, which are left unchanged, because their conversion has failed
    };

    /// Creates the bitonal document. This function is executed in the worker thread,
    /// it must not touch the state of the dialog. The result is converted, if at least
    /// one item has been converted and the resulting document is valid.
    ConversionResult createBitonalDocument(const ConversionSettings& settings);

    void onCreateBitonalDocumentButtonClicked();
    void onPerformFinished();
    void onConversionSourceChanged();
    void onConversionSettingsChanged();
    void onConversionModeChanged();

    /// Displays the menu, which sets the conversion mode of the selected items. When
    /// nothing is selected, the menu is applied to all items in the list.
    /// \param pos Position of the request in the coordinates of the viewport
    void onItemListContextMenuRequested(const QPoint& pos);

    /// Sets the conversion mode to the selected items, or to all items, when nothing
    /// is selected. Items, whose thumbnail could not be generated, cannot be converted
    /// by the algorithm, so the algorithm mode is not applied to them.
    /// \param mode Mode to be set
    void setConversionModeToItems(ConversionItemInfo::Mode mode);

    /// Creates the items of the list. Items are created for all images / pages at
    /// once, so the user sees the whole list immediately, and their thumbnails are
    /// then generated in the background.
    void loadItems();

    void createImageItems(QSize itemSize);
    void createPageItems(QSize itemSize);

    /// Starts the generation of the thumbnails of all items in the background
    /// \param thumbnailSize Maximal size of the generated thumbnails
    void startThumbnailGeneration(QSize thumbnailSize);

    /// Generates the thumbnails of the items. This function is executed in a worker
    /// thread, it must not touch the state of the dialog - each thumbnail is delivered
    /// into the GUI thread using the signal \p thumbnailReady.
    void generateThumbnails(int generation, const ThumbnailRequest& request, const pdf::PDFOperationControl* operationControl);

    /// Generates the preview images. This function is executed in a worker thread,
    /// it must not touch the state of the dialog - the images are delivered into the
    /// GUI thread using the signal \p previewReady.
    void generatePreview(int generation, const PreviewRequest& request, const pdf::PDFOperationControl* operationControl);

    void onThumbnailReady(int generation, int itemIndex, QImage thumbnail);
    void onPreviewReady(int generation, QImage originalImage, QImage bitonalImage);

    /// Marks the items, whose thumbnail has not arrived before the thumbnail job has
    /// ended, as failed. Without this, an item, on which the worker has failed, would
    /// stay pending forever, while the conversion would already be enabled.
    void finishPendingThumbnails();

    /// Takes a snapshot of the settings of the preview and starts the job creating it.
    /// It is called by the timer, so a series of quick changes (holding the arrow of a
    /// spin box) starts a single job instead of one job per change.
    void startPreviewGeneration();

    /// Returns the text displayed below the thumbnail of an item
    QString getItemCaption(int itemIndex) const;

    /// Returns the resolution, at which a page is rasterized for the preview. The
    /// preview pane is small, so rasterizing the page at the full output resolution
    /// would spend hundreds of megabytes on the details, which cannot be displayed
    /// at all. The result of the thresholding depends on the resolution, so a reduced
    /// preview is announced in the caption of the pane.
    /// \param pageIndex Index of the previewed page
    /// \param dpiResolution Resolution selected for the converted document
    int getPreviewDpiResolution(pdf::PDFInteger pageIndex, int dpiResolution) const;

    void updateUi();
    void updatePreview();
    pdf::PDFImageConversion::ConversionMethod getSelectedConversionMethod() const;
    ConversionSource getSelectedConversionSource() const;

    /// Creates a snapshot of the current settings of the dialog
    ConversionSettings getConversionSettings() const;

    Ui::PDFCreateBitonalDocumentDialog* ui;
    const pdf::PDFDocument* m_document;
    pdf::PDFDrawWidgetProxy* m_proxy;
    const pdf::PDFCMS* m_cms;
    QPushButton* m_createBitonalDocumentButton;
    bool m_conversionInProgress;
    QFuture<ConversionResult> m_future;
    std::optional<QFutureWatcher<ConversionResult>> m_futureWatcher;
    pdf::PDFDocument m_bitonalDocument;
    std::vector<pdf::PDFObjectReference> m_imageReferences;
    std::vector<ConversionItemInfo> m_itemsToBeConverted;

    QImage m_previewImageLeft;
    QImage m_previewImageRight;

    PDFCreateBitonalDocumentPreviewWidget* m_leftPreviewWidget;
    PDFCreateBitonalDocumentPreviewWidget* m_rightPreviewWidget;

    pdf::PDFProgress* m_progress;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
    pdf::PDFRasterizerPool* m_rasterizerPool;

    /// Performs all work with the document. It is used both by the GUI thread and by
    /// the worker threads - only the function creating the bitonal document modifies
    /// its state and it never runs together with anything else, because the conversion
    /// disables the whole dialog.
    std::optional<pdf::PDFBitonalDocumentCreator> m_creator;

    /// Conversion source, which the list of items and the preview are showing. It is
    /// used by the GUI thread only, the worker thread gets its own copy in the settings.
    ConversionSource m_conversionSource = ConversionSource::Images;

    /// Job generating the thumbnails of the items in the list
    AsyncJob m_thumbnailJob;

    /// Job generating the preview of the selected item
    AsyncJob m_previewJob;

    /// Delays the start of the preview job, so quickly repeated changes of the settings
    /// do not start a job for each of them
    QTimer m_previewUpdateTimer;

    /// True, when the newest run of the preview job has delivered its images. A worker,
    /// which ends without a result, must not leave the preview panes generating forever.
    bool m_isPreviewDelivered = false;

    /// Page and resolution, which the currently running preview job is rendering. Both
    /// are used by the GUI thread only - they identify the rasterized page returned by
    /// the job, so it can be stored into the cache.
    pdf::PDFInteger m_previewPageIndex = -1;
    int m_previewDpiResolution = 0;

    /// Cache of the rasterized page used in the preview, so changing the conversion
    /// settings does not rasterize the same page again and again
    QImage m_cachedPageImage;
    pdf::PDFInteger m_cachedPageIndex = -1;
    int m_cachedPageDpiResolution = 0;
};

class ImagePreviewDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    ImagePreviewDelegate(std::vector<PDFCreateBitonalDocumentDialog::ConversionItemInfo>* conversionItemInfos, QObject* parent);

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    virtual bool editorEvent(QEvent* event,
                             QAbstractItemModel* model,
                             const QStyleOptionViewItem& option,
                             const QModelIndex& index) override;

    virtual bool helpEvent(QHelpEvent* event,
                           QAbstractItemView* view,
                           const QStyleOptionViewItem& option,
                           const QModelIndex& index) override;

signals:
    /// Emitted, when the user changes the conversion mode of an item. The result of
    /// the previous run does not match the new settings anymore.
    void conversionModeChanged();

private:
    using Mode = PDFCreateBitonalDocumentDialog::ConversionItemInfo::Mode;

    static constexpr QSize s_iconSize = QSize(24, 24);

    /// Modes offered by the marks of an item, in the order in which they are painted
    static constexpr Mode s_modes[] = { Mode::Algorithm, Mode::Original, Mode::FillBlack, Mode::FillWhite };

    /// Returns the rectangle of a single mark of an item
    /// \param option Style option of the item
    /// \param modeIndex Index of the mark in \p s_modes
    QRect getMarkRect(const QStyleOptionViewItem& option, size_t modeIndex) const;

    /// Returns the rectangle covering all marks of an item
    QRect getMarksRect(const QStyleOptionViewItem& option) const;

    /// Paints a single mark of an item
    /// \param painter Painter
    /// \param rect Rectangle of the mark
    /// \param mode Mode, which the mark represents
    /// \param isActive True, when the item is being converted using this mode
    /// \param isEnabled True, when the mode can be selected by the user
    void paintMark(QPainter* painter, QRect rect, Mode mode, bool isActive, bool isEnabled) const;

    /// Returns the tool tip of a mark
    QString getModeToolTip(Mode mode) const;

    std::vector<PDFCreateBitonalDocumentDialog::ConversionItemInfo>* m_conversionItemInfos;
};

}   // namespace pdfviewer

#endif // PDFCREATEBITONALDOCUMENTDIALOG_H
