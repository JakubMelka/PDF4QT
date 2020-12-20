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
//    along with Pdf4Qt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFPAINTERUTILS_H
#define PDFPAINTERUTILS_H

#include <QPainter>

namespace pdf
{

/// RAII wrapper for painter save/restore
class PDFPainterStateGuard
{
public:
    explicit inline PDFPainterStateGuard(QPainter* painter) :
        m_painter(painter)
    {
        m_painter->save();
    }

    inline ~PDFPainterStateGuard()
    {
        m_painter->restore();
    }

private:
    QPainter* m_painter;
};

}   // namespace pdf

#endif // PDFPAINTERUTILS_H
