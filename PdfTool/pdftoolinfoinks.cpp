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

#include "pdftoolinfoinks.h"
#include "pdftransparencyrenderer.h"

namespace pdftool
{

static PDFToolInfoInksApplication s_infoInksApplication;

QString PDFToolInfoInksApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "info-inks";

        case Name:
            return PDFToolTranslationContext::tr("Info about used inks");

        case Description:
            return PDFToolTranslationContext::tr("Retrieve information about inks used in a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolInfoInksApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    cmsManager.setSettings(options.cmsSettings);

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("info-inks", PDFToolTranslationContext::tr("Inks"));
    formatter.endl();

    formatter.beginTable("inks", PDFToolTranslationContext::tr("Ink list"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("index", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("id", PDFToolTranslationContext::tr("Identifier"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("name", PDFToolTranslationContext::tr("Name"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("is-spot", PDFToolTranslationContext::tr("Spot"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("color", PDFToolTranslationContext::tr("Color"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    pdf::PDFInkMapper mapper(&cmsManager, &document);
    mapper.createSpotColors(true);
    std::vector<pdf::PDFInkMapper::ColorInfo> colorInfos = mapper.getSeparations(4, true);

    QLocale locale;

    int colorIndex = 0;
    for (const pdf::PDFInkMapper::ColorInfo& colorInfo : colorInfos)
    {
        formatter.beginTableRow("ink", ++colorIndex);
        formatter.writeTableColumn("index", locale.toString(colorIndex), Qt::AlignRight);
        formatter.writeTableColumn("id", colorInfo.name.toPercentEncoding(" "));
        formatter.writeTableColumn("name", colorInfo.textName);
        formatter.writeTableColumn("is-spot", colorInfo.isSpot ? PDFToolTranslationContext::tr("Yes") : PDFToolTranslationContext::tr("No"), Qt::AlignCenter);
        formatter.writeTableColumn("color", colorInfo.color.name(), Qt::AlignLeft);
        formatter.endTableRow();
    };

    formatter.endTable();

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInfoInksApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | ColorManagementSystem;
}


}   // namespace pdf
