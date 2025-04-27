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

#ifndef PDFTOOLRENDER_H
#define PDFTOOLRENDER_H

#include "pdftoolabstractapplication.h"
#include "pdfexception.h"

namespace pdftool
{

class PDFToolRenderBase : public PDFToolAbstractApplication
{
public:
    virtual int execute(const PDFToolOptions& options) override;

protected:
    virtual void finish(const PDFToolOptions& options) = 0;
    virtual void onPageRendered(const PDFToolOptions& options, pdf::PDFRenderedPageImage& renderedPageImage) = 0;

    void writePageInfoStatistics(const pdf::PDFRenderedPageImage& renderedPageImage);

    void writeStatistics(PDFOutputFormatter& formatter);
    void writePageStatistics(PDFOutputFormatter& formatter);
    void writeErrors(PDFOutputFormatter& formatter);

    struct PageInfo
    {
        bool isRendered = false;
        pdf::PDFInteger pageIndex = 0;
        qint64 pageCompileTime = 0;
        qint64 pageWaitTime = 0;
        qint64 pageRenderTime = 0;
        qint64 pageTotalTime = 0;
        qint64 pageWriteTime = 0;
        std::vector<pdf::PDFRenderError> errors;
    };

    std::vector<PageInfo> m_pageInfo;
    qint64 m_wallTime = 0;
};

class PDFToolRender : public PDFToolRenderBase
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual Options getOptionsFlags() const override;

protected:
    virtual void finish(const PDFToolOptions& options) override;
    virtual void onPageRendered(const PDFToolOptions& options, pdf::PDFRenderedPageImage& renderedPageImage) override;
};

class PDFToolBenchmark : public PDFToolRenderBase
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual Options getOptionsFlags() const override;

protected:
    virtual void finish(const PDFToolOptions& options) override;
    virtual void onPageRendered(const PDFToolOptions& options, pdf::PDFRenderedPageImage& renderedPageImage) override;
};

}   // namespace pdftool

#endif // PDFTOOLRENDER_H
