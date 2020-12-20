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

#ifndef PDFTOOLFETCHIMAGES_H
#define PDFTOOLFETCHIMAGES_H

#include "pdftoolabstractapplication.h"

#include <QMutex>

namespace pdftool
{

class PDFToolFetchImages : public PDFToolAbstractApplication
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;

    void onImageExtracted(pdf::PDFInteger pageIndex, pdf::PDFInteger order, const QImage& image);

private:
    struct Image
    {
        QByteArray hash;
        pdf::PDFInteger pageIndex = 0;
        pdf::PDFInteger order = 0;
        QImage image;
        QString fileName;
    };
    using Images = std::vector<Image>;

    QMutex m_mutex;
    Images m_images;
};

}   // namespace pdftool

#endif // PDFTOOLFETCHIMAGES_H
