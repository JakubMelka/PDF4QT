//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFRENDERTOIMAGESDIALOG_H
#define PDFRENDERTOIMAGESDIALOG_H

#include "pdfrenderer.h"

#include <QDialog>

namespace Ui
{
class PDFRenderToImagesDialog;
}

namespace pdfviewer
{

class PDFRenderToImagesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFRenderToImagesDialog(QWidget* parent);
    virtual ~PDFRenderToImagesDialog() override;

private slots:
    void on_selectDirectoryButton_clicked();

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

    Ui::PDFRenderToImagesDialog* ui;
    pdf::PDFImageWriterSettings m_imageWriterSettings;
    pdf::PDFPageImageExportSettings m_imageExportSettings;
    bool m_isLoadingData;
};

}   // namespace pdfviewer

#endif // PDFRENDERTOIMAGESDIALOG_H
