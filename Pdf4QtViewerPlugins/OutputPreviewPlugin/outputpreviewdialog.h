//    Copyright (C) 2021 Jakub Melka
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

    QFuture<RenderedImage> m_future;
    QFutureWatcher<RenderedImage>* m_futureWatcher;
};

}   // namespace pdf

#endif // OUTPUTPREVIEWDIALOG_H
