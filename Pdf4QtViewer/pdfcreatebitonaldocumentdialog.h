//    Copyright (C) 2023 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

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

}   // namespace pdfviewer

#endif // PDFCREATEBITONALDOCUMENTDIALOG_H
