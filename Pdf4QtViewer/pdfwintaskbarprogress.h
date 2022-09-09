//    Copyright (C) 2022 Jakub Melka
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

#ifndef PDFWINTASKBARPROGRESS_H
#define PDFWINTASKBARPROGRESS_H

#include "pdfviewerglobal.h"
#include "pdfglobal.h"

#include <QObject>

class QWindow;

namespace pdfviewer
{

class PDFWinTaskBarProgressImpl;

/// Windows task bar progress. Displays progress
/// in the task bar icon in window's bottom side.
class PDFWinTaskBarProgress : public QObject
{
    Q_OBJECT

public:
    PDFWinTaskBarProgress(QObject* parent);
    virtual ~PDFWinTaskBarProgress() override;

    void setWindow(QWindow* window);

    void reset();
    void show();
    void hide();

    void setValue(pdf::PDFInteger value);
    void setRange(pdf::PDFInteger min, pdf::PDFInteger max);

private:
    PDFWinTaskBarProgressImpl* m_impl;
};

}   // namespace pdfviewer

#endif // PDFWINTASKBARPROGRESS_H
