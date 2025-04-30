// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

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
