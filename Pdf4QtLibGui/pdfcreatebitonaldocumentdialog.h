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

#include "pdfcms.h"
#include "pdfdocument.h"
#include "pdfobjectutils.h"
#include "pdfimage.h"
#include "pdfimageconversion.h"
#include "pdfprogress.h"

#include <QDialog>
#include <QFuture>
#include <QSvgRenderer>
#include <QFutureWatcher>
#include <QStyledItemDelegate>

#include <functional>
#include <vector>

namespace Ui
{
class PDFCreateBitonalDocumentDialog;
}

namespace pdf
{
class PDFPage;
class PDFRasterizerPool;
class PDFDocumentBuilder;
class PDFDrawWidgetProxy;
class PDFOptionalContentActivity;
}

namespace pdfviewer
{

class PDFCreateBitonalDocumentPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    PDFCreateBitonalDocumentPreviewWidget(QWidget* parent);
    virtual ~PDFCreateBitonalDocumentPreviewWidget() override;

    virtual void paintEvent(QPaintEvent* event) override;

    void setCaption(QString caption);
    void setImage(QImage image);

private:
    QString m_caption;
    QImage m_image;
};

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

    /// Source of the bitonal conversion. Documents produced by scanners often
    /// store a single scanned page as several images (for example a background
    /// image and a text layer masked by a stencil mask). Converting such images
    /// one by one cannot produce a reasonable bitonal page, so the whole page
    /// composition can be converted instead.
    enum class ConversionSource
    {
        Images, ///< Each image of the document is converted separately
        Pages   ///< Whole pages are rasterized and converted as a single image
    };

    struct ConversionItemInfo
    {
        pdf::PDFObjectReference imageReference;  ///< Valid, when images are converted
        pdf::PDFInteger pageIndex = -1;          ///< Valid, when pages are converted
        bool conversionEnabled = true;
    };

    /// Immutable snapshot of all inputs of the conversion. It is created in the GUI
    /// thread and a copy of it is owned by the worker thread, so the worker never
    /// reads a setting, which the user can change in the meantime.
    struct ConversionSettings
    {
        ConversionSource conversionSource = ConversionSource::Images;
        pdf::PDFImageConversion::ConversionMethod conversionMethod = pdf::PDFImageConversion::ConversionMethod::Automatic;
        int manualThreshold = 128;
        int dpiResolution = 300;
        std::vector<ConversionItemInfo> items;
    };

private:
    /// Creates the bitonal document. This function is executed in the worker thread,
    /// it must not touch the state of the dialog. Returns true, if at least one item
    /// has been converted and the resulting document is valid.
    bool createBitonalDocument(const ConversionSettings& settings);

    bool createBitonalDocumentFromImages(pdf::PDFDocumentBuilder& builder, const ConversionSettings& settings);
    bool createBitonalDocumentFromPages(pdf::PDFDocumentBuilder& builder, const ConversionSettings& settings);
    void onCreateBitonalDocumentButtonClicked();
    void onPerformFinished();
    void onConversionSourceChanged();
    void onConversionSettingsChanged();
    void onConversionEnabledChanged();
    void loadItems();
    void loadImageItems(QSize thumbnailSize);
    void loadPageItems(QSize thumbnailSize);

    /// Throws away the document created by the previous run, because it has been
    /// created with different settings than which are displayed now. Without this,
    /// the user could accept a document, which does not match the dialog.
    void invalidateResult();

    void updateUi();
    void updatePreview();
    pdf::PDFImageConversion::ConversionMethod getSelectedConversionMethod() const;
    ConversionSource getSelectedConversionSource() const;

    /// Creates a snapshot of the current settings of the dialog
    ConversionSettings getConversionSettings() const;

    /// Converts the image to the bitonal one. Returns a null image, if the conversion fails.
    /// \param image Image to be converted
    /// \param conversionMethod Conversion method
    /// \param threshold Manual threshold
    /// \param alphaMask Transparency of the converted image (can be a null image)
    static QImage convertImageToBitonal(const QImage& image,
                                        pdf::PDFImageConversion::ConversionMethod conversionMethod,
                                        int threshold,
                                        QImage* alphaMask);

    /// Creates an image object (1 bit per component, DeviceGray) from a bitonal image
    /// \param image Bitonal image
    static pdf::PDFObject createBitonalImageObject(const QImage& image);

    /// Rasterizes the given pages and calls the processor for each rendered page
    /// image. Images are composited onto the white background and they are returned
    /// in the coordinate system of the page, i.e. the page rotation is not applied
    /// to them. The processor can be called from multiple threads simultaneously.
    /// \param pageIndices Indices of the rendered pages
    /// \param pageSizeGetter Functor returning the size of the rendered page image
    /// \param pageImageProcessor Functor processing the rendered page image
    void renderPages(const std::vector<pdf::PDFInteger>& pageIndices,
                     const std::function<QSize(const pdf::PDFPage*)>& pageSizeGetter,
                     const std::function<void(pdf::PDFInteger, QImage)>& pageImageProcessor) const;

    /// Rasterizes a single page into an image of a given size. \sa renderPages
    /// \param pageIndex Index of the rendered page
    /// \param size Size of the target image
    QImage renderPage(pdf::PDFInteger pageIndex, QSize size) const;

    /// Returns size of the rasterized page image for a given resolution
    /// \param page Page
    /// \param dpiResolution Resolution in dots per inch
    static QSize getPageImageSize(const pdf::PDFPage* page, int dpiResolution);

    /// Estimates the resolution of the document, so the rasterized pages do not lose
    /// the details of the scanned images. Resolution is estimated from the size of the
    /// images used on the pages. Because it is not known, which part of the page an
    /// image actually covers, the estimate is only used to raise the resolution above
    /// the default one - the returned value is never lower than the default resolution.
    int getEstimatedDpiResolution() const;

    /// Returns true, if the image is a stencil mask. Stencil masks are already
    /// bitonal and they are painted using the current fill color, so it does not
    /// make sense to convert them.
    bool isStencilMask(pdf::PDFObjectReference reference) const;

    std::optional<pdf::PDFImage> getImageFromReference(pdf::PDFObjectReference reference) const;

    Ui::PDFCreateBitonalDocumentDialog* ui;
    const pdf::PDFDocument* m_document;
    pdf::PDFDrawWidgetProxy* m_proxy;
    const pdf::PDFCMS* m_cms;
    QPushButton* m_createBitonalDocumentButton;
    bool m_conversionInProgress;
    bool m_processed;
    QFuture<bool> m_future;
    std::optional<QFutureWatcher<bool>> m_futureWatcher;
    pdf::PDFDocument m_bitonalDocument;
    pdf::PDFObjectClassifier m_classifier;
    std::vector<pdf::PDFObjectReference> m_imageReferences;
    std::vector<ConversionItemInfo> m_itemsToBeConverted;

    QImage m_previewImageLeft;
    QImage m_previewImageRight;

    PDFCreateBitonalDocumentPreviewWidget* m_leftPreviewWidget;
    PDFCreateBitonalDocumentPreviewWidget* m_rightPreviewWidget;

    pdf::PDFProgress* m_progress;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
    pdf::PDFRasterizerPool* m_rasterizerPool;

    /// Conversion source, which the list of items and the preview are showing. It is
    /// used by the GUI thread only, the worker thread gets its own copy in the settings.
    ConversionSource m_conversionSource = ConversionSource::Images;

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
    /// Emitted, when the user turns the conversion of an item on or off. The result
    /// of the previous run does not match the new selection anymore.
    void conversionEnabledChanged();

private:
    static constexpr QSize s_iconSize = QSize(24, 24);

    QRect getMarkRect(const QStyleOptionViewItem& option) const;

    std::vector<PDFCreateBitonalDocumentDialog::ConversionItemInfo>* m_conversionItemInfos;
    mutable QSvgRenderer m_yesRenderer;
    mutable QSvgRenderer m_noRenderer;
};

}   // namespace pdfviewer

#endif // PDFCREATEBITONALDOCUMENTDIALOG_H
