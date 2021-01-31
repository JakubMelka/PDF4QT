//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef OUTPUTPREVIEWDIALOG_H
#define OUTPUTPREVIEWDIALOG_H

#include "pdfdocument.h"
#include "pdfdrawwidget.h"
#include "pdftransparencyrenderer.h"

#include <QDialog>

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

private:
    void updateImage();

    Ui::OutputPreviewDialog* ui;
    pdf::PDFInkMapper m_inkMapper;
    const pdf::PDFDocument* m_document;
    pdf::PDFWidget* m_widget;
};

}   // namespace pdf

#endif // OUTPUTPREVIEWDIALOG_H
