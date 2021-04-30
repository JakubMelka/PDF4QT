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
