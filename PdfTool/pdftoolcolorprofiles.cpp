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
