//    Copyright (C) 2019 Jakub Melka
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

#ifndef PDFDOCUMENTPROPERTIESDIALOG_H
#define PDFDOCUMENTPROPERTIESDIALOG_H

#include "pdfglobal.h"

#include <QDialog>

namespace Ui
{
class PDFDocumentPropertiesDialog;
}

namespace pdf
{
class PDFDocument;
}

namespace pdfviewer
{

struct PDFFileInfo
{
    QString fileName;
    QString path;
    pdf::PDFInteger fileSize = 0;
    bool writable = false;
    QDateTime creationTime;
    QDateTime lastModifiedTime;
    QDateTime lastReadTime;
};

class PDFDocumentPropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFDocumentPropertiesDialog(const pdf::PDFDocument* document,
                                         const PDFFileInfo* fileInfo,
                                         QWidget* parent);
    virtual ~PDFDocumentPropertiesDialog() override;

private:
    Ui::PDFDocumentPropertiesDialog* ui;

    void initializeProperties(const pdf::PDFDocument* document);
    void initializeFileInfoProperties(const PDFFileInfo* fileInfo);
    void initializeSecurity(const pdf::PDFDocument* document);
};

}   // namespace pdfviewer

#endif // PDFDOCUMENTPROPERTIESDIALOG_H
