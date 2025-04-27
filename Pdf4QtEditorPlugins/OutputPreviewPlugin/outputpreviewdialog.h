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

#ifndef OUTPUTPREVIEWDIALOG_H
#define OUTPUTPREVIEWDIALOG_H

#include "pdfdocument.h"
#include "pdfdrawwidget.h"
#include "pdftransparencyrenderer.h"
#include "outputpreviewwidget.h"

#include <QDialog>
#include <QFuture>
#include <QFutureWatcher>

namespace Ui
{
class OutputPreviewDialog;
}

namespace pdfplugin
{

class OutputPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OutputPreviewDialog(const pdf::PDFDocument* document, pdf::PDFWidget* widget, QWidget* parent);
    virtual ~OutputPreviewDialog() override;

    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void closeEvent(QCloseEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

    virtual void accept() override;
    virtual void reject() override;

private:
    void updateInks();
    void updatePaperColorWidgets();
    void updateAlarmColorButtonIcon();

    void onPaperColorChanged();
    void onAlarmColorButtonClicked();
    void onSimulateSeparationsChecked(bool checked);
    void onSimulatePaperColorChecked(bool checked);
    void onDisplayModeChanged();
    void onInksChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles);
    void onInkCoverageLimitChanged(double value);
    void onRichBlackLimtiChanged(double value);

    struct RenderedImage
    {
        QImage image;
        pdf::PDFFloatBitmapWithColorSpace originalProcessImage;
        QSizeF pageSize;
        QList<pdf::PDFRenderError> errors;
    };

    void updatePageImage();
    void onPageImageRendered();
    RenderedImage renderPage(const pdf::PDFPage* page,
                             QSize renderSize,
                             pdf::PDFRGB paperColor,
                             uint32_t activeColorMask,
                             pdf::PDFTransparencyRendererSettings::Flags additionalFlags);
    bool isRenderingDone() const;

    Ui::OutputPreviewDialog* ui;
    pdf::PDFInkMapper m_inkMapper;
    pdf::PDFInkMapper m_inkMapperForRendering;
    const pdf::PDFDocument* m_document;
    pdf::PDFWidget* m_widget;
    bool m_needUpdateImage;
    OutputPreviewWidget* m_outputPreviewWidget;

    QFuture<RenderedImage> m_future;
    QFutureWatcher<RenderedImage>* m_futureWatcher;
};

}   // namespace pdf

#endif // OUTPUTPREVIEWDIALOG_H
