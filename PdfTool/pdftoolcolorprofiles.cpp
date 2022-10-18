//    Copyright (C) 2020-2021 Jakub Melka
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

#include "pdftoolcolorprofiles.h"
#include "pdfcms.h"

namespace pdftool
{

static PDFToolColorProfiles s_colorProfilesApplication;

QString PDFToolColorProfiles::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "color-profiles";

        case Name:
            return PDFToolTranslationContext::tr("Color Profiles List");

        case Description:
            return PDFToolTranslationContext::tr("Show list of available color profiles.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolColorProfiles::execute(const PDFToolOptions& options)
{
    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("color-profiles", PDFToolTranslationContext::tr("Available Color Profiles"));
    formatter.endl();

    auto writeColorProfileList = [&formatter](QString name, QString caption, const pdf::PDFColorProfileIdentifiers& profiles)
    {
        if (profiles.empty())
        {
            // Jakub Melka: Do nothing, no profile available
            return;
        }

        formatter.beginTable(name, caption);

        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("name", PDFToolTranslationContext::tr("Name"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("id", PDFToolTranslationContext::tr("Identifier"), Qt::AlignLeft);
        formatter.endTableHeaderRow();

        QLocale locale;

        int ref = 1;
        for (const pdf::PDFColorProfileIdentifier& profile : profiles)
        {
            formatter.beginTableRow("color-profile", ref);

            formatter.writeTableColumn("no", locale.toString(ref), Qt::AlignRight);
            formatter.writeTableColumn("name", profile.name);
            formatter.writeTableColumn("id", profile.id);

            formatter.endTableRow();
            ++ref;
        }

        formatter.endTable();
        formatter.endl();
    };

    pdf::PDFCMSManager cmsManager(nullptr);
    writeColorProfileList("output-profiles", PDFToolTranslationContext::tr("Output Profiles"), cmsManager.getOutputProfiles());
    writeColorProfileList("gray-profiles", PDFToolTranslationContext::tr("Gray Profiles"), cmsManager.getGrayProfiles());
    writeColorProfileList("rgb-profiles", PDFToolTranslationContext::tr("RGB Profiles"), cmsManager.getRGBProfiles());
    writeColorProfileList("cmyk-profiles", PDFToolTranslationContext::tr("CMYK Profiles"), cmsManager.getCMYKProfiles());

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolColorProfiles::getOptionsFlags() const
{
    return ConsoleFormat;
}

}   // namespace pdftool
