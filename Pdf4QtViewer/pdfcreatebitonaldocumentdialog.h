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

#include "pdfdocument.h"

#include <QDialog>
#include <QFuture>

namespace Ui
{
class PDFCreateBitonalDocumentDialog;
}

namespace pdfviewer
{

class PDFCreateBitonalDocumentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFCreateBitonalDocumentDialog(const pdf::PDFDocument* document, QWidget* parent);
    virtual ~PDFCreateBitonalDocumentDialog() override;

    pdf::PDFDocument takeBitonaldDocument() { return qMove(m_bitonalDocument); }

private:
    void createBitonalDocument();
    void onCreateBitonalDocumentButtonClicked();

    void updateUi();

    Ui::PDFCreateBitonalDocumentDialog* ui;
    const pdf::PDFDocument* m_document;
    QPushButton* m_createBitonalDocumentButton;
    bool m_conversionInProgress;
    bool m_processed;
    QFuture<void> m_future;
    pdf::PDFDocument m_bitonalDocument;
};

}   // namespace pdfviewer

#endif // PDFCREATEBITONALDOCUMENTDIALOG_H
