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

#include "pdftoolinfopageboxes.h"
#include "pdfutils.h"

namespace pdftool
{

static PDFToolInfoPageBoxesApplication s_infoPageBoxesApplication;

QString PDFToolInfoPageBoxesApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "info-page-boxes";

        case Name:
            return PDFToolTranslationContext::tr("Info (page boxes)");

        case Description:
            return PDFToolTranslationContext::tr("Retrieve informations about page boxes in a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

struct PDFPageBoxInfo
{
    bool operator==(const PDFPageBoxInfo& other) const
    {
        return mediaBox == other.mediaBox &&
                cropBox == other.cropBox &&
                bleedBox == other.bleedBox &&
                trimBox == other.trimBox &&
                artBox == other.artBox;
    }

    pdf::PDFClosedIntervalSet pages;
    QRectF mediaBox;
    QRectF cropBox;
    QRectF bleedBox;
    QRectF trimBox;
    QRectF artBox;
};

int PDFToolInfoPageBoxesApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pages = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

    if (!parseError.isEmpty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    std::vector<PDFPageBoxInfo> infos;
    for (const pdf::PDFInteger pageIndex : pages)
    {
        const pdf::PDFPage* page = document.getCatalog()->getPage(pageIndex);

        PDFPageBoxInfo info;
        info.mediaBox = page->getMediaBoxMM();
        info.cropBox = page->getCropBoxMM();
        info.bleedBox = page->getBleedBoxMM();
        info.trimBox = page->getTrimBoxMM();
        info.artBox = page->getArtBoxMM();

        auto it = std::find(infos.begin(), infos.end(), info);
        if (it != infos.end())
        {
            it->pages.addValue(pageIndex + 1);
        }
        else
        {
            info.pages.addValue(pageIndex + 1);
            infos.emplace_back(qMove(info));
        }
    }

    QLocale locale;

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("info-page-boxes", PDFToolTranslationContext::tr("Page boxes in document %1").arg(options.document));

    auto writeBox = [&formatter, &locale](const QString& name, const QString& title, const QRectF& rect)
    {
        formatter.beginTableRow(name);
        formatter.writeTableColumn("title", title);

        if (rect.isValid())
        {
            formatter.writeTableColumn("value", QString("[ %1 %2 %3 %4 ]").arg(locale.toString(rect.left()), locale.toString(rect.top()), locale.toString(rect.right()), locale.toString(rect.bottom())));
        }
        else
        {
            formatter.writeTableColumn("value", "null");
        }

        formatter.endTableRow();
    };

    for (const PDFPageBoxInfo& info : infos)
    {
        formatter.endl();
        formatter.beginTable("page-range", PDFToolTranslationContext::tr("Pages %1").arg(info.pages.toText(true)));

        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("box", PDFToolTranslationContext::tr("Box"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("value", PDFToolTranslationContext::tr("Value"), Qt::AlignLeft);
        formatter.endTableHeaderRow();

        writeBox("media", PDFToolTranslationContext::tr("Media"), info.mediaBox);
        writeBox("crop", PDFToolTranslationContext::tr("Crop"), info.cropBox);
        writeBox("bleed", PDFToolTranslationContext::tr("Bleed"), info.bleedBox);
        writeBox("trim", PDFToolTranslationContext::tr("Trim"), info.trimBox);
        writeBox("art", PDFToolTranslationContext::tr("Art"), info.artBox);

        formatter.endTable();
    }

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInfoPageBoxesApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector;
}

}   // namespace pdftool
