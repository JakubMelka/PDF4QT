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

#ifndef PDFTOOLVERIFYSIGNATURES_H
#define PDFTOOLVERIFYSIGNATURES_H

#include "pdftoolabstractapplication.h"

namespace pdftool
{

class PDFToolVerifySignaturesApplication : public PDFToolAbstractApplication
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;
};

}   // namespace pdftool

#endif // PDFTOOLVERIFYSIGNATURES_H
