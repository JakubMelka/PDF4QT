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

namespace Ui
{
class PDFCreateBitonalDocumentDialog;
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
                                            const pdf::PDFCMS* cms,
                                            pdf::PDFProgress* progress,
                                            QWidget* parent);
    virtual ~PDFCreateBitonalDocumentDialog() override;

    pdf::PDFDocument takeBitonaldDocument() { return qMove(m_bitonalDocument); }

    struct ImageConversionInfo
    {
        pdf::PDFObjectReference imageReference;
        bool conversionEnabled = true;
    };

private:
    void createBitonalDocument();
    void onCreateBitonalDocumentButtonClicked();
    void onPerformFinished();
    void loadImages();

    void updateUi();
    void updatePreview();

    std::optional<pdf::PDFImage> getImageFromReference(pdf::PDFObjectReference reference) const;

    Ui::PDFCreateBitonalDocumentDialog* ui;
    const pdf::PDFDocument* m_document;
    const pdf::PDFCMS* m_cms;
    QPushButton* m_createBitonalDocumentButton;
    bool m_conversionInProgress;
    bool m_processed;
    QFuture<void> m_future;
    std::optional<QFutureWatcher<void>> m_futureWatcher;
    pdf::PDFDocument m_bitonalDocument;
    pdf::PDFObjectClassifier m_classifier;
    std::vector<pdf::PDFObjectReference> m_imageReferences;
    std::vector<ImageConversionInfo> m_imagesToBeConverted;

    QImage m_previewImageLeft;
    QImage m_previewImageRight;

    PDFCreateBitonalDocumentPreviewWidget* m_leftPreviewWidget;
    PDFCreateBitonalDocumentPreviewWidget* m_rightPreviewWidget;

    pdf::PDFProgress* m_progress;

    pdf::PDFImageConversion::ConversionMethod m_conversionMethod = pdf::PDFImageConversion::ConversionMethod::Automatic;
    int m_manualThreshold = 128;
};

class ImagePreviewDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    ImagePreviewDelegate(std::vector<PDFCreateBitonalDocumentDialog::ImageConversionInfo>* imageConversionInfos, QObject* parent);

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    virtual bool editorEvent(QEvent* event,
                             QAbstractItemModel* model,
                             const QStyleOptionViewItem& option,
                             const QModelIndex& index) override;

    virtual bool helpEvent(QHelpEvent* event,
                           QAbstractItemView* view,
                           const QStyleOptionViewItem& option,
                           const QModelIndex& index) override;

private:
    static constexpr QSize s_iconSize = QSize(24, 24);

    QRect getMarkRect(const QStyleOptionViewItem& option) const;

    std::vector<PDFCreateBitonalDocumentDialog::ImageConversionInfo>* m_imageConversionInfos;
    mutable QSvgRenderer m_yesRenderer;
    mutable QSvgRenderer m_noRenderer;
};

}   // namespace pdfviewer

#endif // PDFCREATEBITONALDOCUMENTDIALOG_H
