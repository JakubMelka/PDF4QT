//    Copyright (C) 2020-2021 Jakub Melka
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

class PDF4QTLIBSHARED_EXPORT PDFSelectPagesDialog : public QDialog
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
