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

#ifndef PDFFONT_H
#define PDFFONT_H

#include "pdfglobal.h"

#include <QSharedPointer>

namespace pdf
{

enum class TextRenderingMode
{
    Fill = 0,
    Stroke = 1,
    FillStroke = 2,
    Invisible = 3,
    FillClip = 4,
    StrokeClip = 5,
    FillStrokeClip = 6,
    Clip = 7
};

constexpr bool isTextRenderingModeFilled(TextRenderingMode mode)
{
    switch (mode)
    {
        case TextRenderingMode::Fill:
        case TextRenderingMode::FillClip:
        case TextRenderingMode::FillStroke:
        case TextRenderingMode::FillStrokeClip:
            return true;

        default:
            return false;
    }
}

constexpr bool isTextRenderingModeStroked(TextRenderingMode mode)
{
    switch (mode)
    {
        case TextRenderingMode::Stroke:
        case TextRenderingMode::FillStroke:
        case TextRenderingMode::StrokeClip:
        case TextRenderingMode::FillStrokeClip:
            return true;

        default:
            return false;
    }
}

constexpr bool isTextRenderingModeClipped(TextRenderingMode mode)
{
    switch (mode)
    {
        case TextRenderingMode::Clip:
        case TextRenderingMode::FillClip:
        case TextRenderingMode::StrokeClip:
        case TextRenderingMode::FillStrokeClip:
            return true;

        default:
            return false;
    }
}

class PDFFont
{
public:
    PDFFont();
};

using PDFFontPointer = QSharedPointer<PDFFont>;

}   // namespace pdf

#endif // PDFFONT_H
