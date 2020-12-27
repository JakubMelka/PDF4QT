//    Copyright (C) 2020 Jakub Melka
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

#ifndef SELECTPAGESTOREDACTDIALOG_H
#define SELECTPAGESTOREDACTDIALOG_H

#include "pdfutils.h"

#include <QDialog>

namespace Ui
{
class SelectPagesToRedactDialog;
}

namespace pdfplugin
{

class SelectPagesToRedactDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SelectPagesToRedactDialog(pdf::PDFInteger pageCount, const std::vector<pdf::PDFInteger>& visiblePages, QWidget* parent);
    virtual ~SelectPagesToRedactDialog() override;

    virtual void accept() override;

    std::vector<pdf::PDFInteger> getSelectedPages() const;

private:
    void updateUi();

    Ui::SelectPagesToRedactDialog* ui;
    pdf::PDFInteger m_pageCount;
    std::vector<pdf::PDFInteger> m_visiblePages;
    std::vector<pdf::PDFInteger> m_evenPages;
    std::vector<pdf::PDFInteger> m_oddPages;


};

}   // namespace pdfplugin

#endif // SELECTPAGESTOREDACTDIALOG_H
