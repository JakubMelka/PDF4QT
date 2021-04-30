//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFSELECTPAGESDIALOG_H
#define PDFSELECTPAGESDIALOG_H

#include "pdfutils.h"

#include <QDialog>

namespace Ui
{
class PDFSelectPagesDialog;
}

namespace pdf
{

class Pdf4QtLIBSHARED_EXPORT PDFSelectPagesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFSelectPagesDialog(QString windowTitle,
                                  QString groupBoxTitle,
                                  pdf::PDFInteger pageCount,
                                  const std::vector<pdf::PDFInteger>& visiblePages,
                                  QWidget* parent);

    virtual ~PDFSelectPagesDialog() override;

    virtual void accept() override;

    std::vector<pdf::PDFInteger> getSelectedPages() const;

private:
    void updateUi();

    Ui::PDFSelectPagesDialog* ui;
    pdf::PDFInteger m_pageCount;
    std::vector<pdf::PDFInteger> m_visiblePages;
    std::vector<pdf::PDFInteger> m_evenPages;
    std::vector<pdf::PDFInteger> m_oddPages;
};

}   // namespace pdf

#endif // PDFSELECTPAGESDIALOG_H
