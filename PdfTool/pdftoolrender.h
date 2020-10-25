//    Copyright (C) 2020 Jakub Melka
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

#ifndef PDFTOOLRENDER_H
#define PDFTOOLRENDER_H

#include "pdftoolabstractapplication.h"

namespace pdftool
{

class PDFToolRenderBase : public PDFToolAbstractApplication
{
public:
    virtual int execute(const PDFToolOptions& options) override;
};

class PDFToolRender : public PDFToolRenderBase
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual Options getOptionsFlags() const override;
};

}   // namespace pdftool

#endif // PDFTOOLRENDER_H
