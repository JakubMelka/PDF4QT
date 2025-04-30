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

#ifndef PDFRENDERTOIMAGESDIALOG_H
#define PDFRENDERTOIMAGESDIALOG_H

#include "pdfcms.h"
#include "pdfrenderer.h"

#include <QDialog>
#include <QFutureWatcher>

class QAbstractButton;

namespace Ui
{
class PDFRenderToImagesDialog;
}

namespace pdf
{
class PDFProgress;
class PDFDrawWidgetProxy;
}

namespace pdfviewer
{

class PDFRenderToImagesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFRenderToImagesDialog(const pdf::PDFDocument* document,
                                     pdf::PDFDrawWidgetProxy* proxy,
                                     pdf::PDFProgress* progress,
                                     QWidget* parent);
    virtual ~PDFRenderToImagesDialog() override;

private slots:
    void on_selectDirectoryButton_clicked();
    void on_buttonBox_clicked(QAbstractButton* button);

private:
    /// Loads image writer settings to the ui
    void loadImageWriterSettings();

    /// Load image export settigns to the ui
    void loadImageExportSettings();

    void onFormatChanged();
    void onSubtypeChanged();
    void onPagesButtonClicked(bool checked);
    void onSelectedPagesChanged(const QString& text);
    void onDirectoryChanged(const QString& text);
    void onFileTemplateChanged(const QString& text);
    void onResolutionButtonClicked(bool checked);
    void onResolutionDPIChanged(int value);
    void onResolutionPixelsChanged(int value);
    void onCompressionChanged(int value);
    void onQualityChanged(int value);
    void onGammaChanged(double value);
    void onOptimizedWriteChanged(bool value);
    void onProgressiveScanWriteChanged(bool value);
    void onRenderError(pdf::PDFInteger pageIndex, pdf::PDFRenderError error);
    void onRenderingFinished();

    Ui::PDFRenderToImagesDialog* ui;
    const pdf::PDFDocument* m_document;
    pdf::PDFDrawWidgetProxy* m_proxy;
    pdf::PDFProgress* m_progress;
    pdf::PDFImageWriterSettings m_imageWriterSettings;
    pdf::PDFPageImageExportSettings m_imageExportSettings;
    bool m_isLoadingData;
    QFutureWatcher<void> m_watcher;

    std::vector<pdf::PDFInteger> m_pageIndices;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
    pdf::PDFCMSPointer m_cms;
    pdf::PDFRasterizerPool* m_rasterizerPool;
};

}   // namespace pdfviewer

Q_DECLARE_METATYPE(pdf::PDFRenderError)

#endif // PDFRENDERTOIMAGESDIALOG_H
