//    Copyright (C) 2021 Jakub Melka
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

    PDFOutputFormatter formatter(options.outputStyle, options.outputCodec);
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
