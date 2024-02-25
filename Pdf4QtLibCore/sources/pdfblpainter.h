//    Copyright (C) 2024 Jakub Melka
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

#ifndef PDFBLPAINTER_H
#define PDFBLPAINTER_H

#include "pdfglobal.h"

#include <QImage>
#include <QPaintDevice>

namespace pdf
{
class PDFBLPaintEngine;

class PDF4QTLIBCORESHARED_EXPORT PDFBLPaintDevice : public QPaintDevice
{
public:
    PDFBLPaintDevice(QImage& offscreenBuffer, bool isMultithreaded);
    virtual ~PDFBLPaintDevice() override;

    virtual int devType() const override;
    virtual QPaintEngine* paintEngine() const override;

    static uint32_t getVersion();

protected:
    virtual int metric(PaintDeviceMetric metric) const override;

private:
    QImage& m_offscreenBuffer;
    PDFBLPaintEngine* m_paintEngine;
};

}   // namespace pdf

#endif // PDFBLPAINTER_H
