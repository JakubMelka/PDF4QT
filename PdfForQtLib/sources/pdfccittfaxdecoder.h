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

#ifndef PDFCCITTFAXDECODER_H
#define PDFCCITTFAXDECODER_H

#include "pdfutils.h"

namespace pdf
{

struct PDFCCITTCode;

class PDFCCITTFaxDecoder
{
public:
    explicit PDFCCITTFaxDecoder(const QByteArray* stream);

private:
    uint32_t getRunLength(bool white);

    uint32_t getWhiteCode();
    uint32_t getBlackCode();

    uint32_t getCode(const PDFCCITTCode* codes, size_t codeCount);

    PDFBitReader m_reader;
};

}   // namespace pdf

#endif // PDFCCITTFAXDECODER_H
