// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdftoolinfofonts.h"
#include "pdfdocument.h"
#include "pdfdocumentreport.h"
#include "pdffont.h"
#include "pdfutils.h"

namespace pdftool
{

static PDFToolInfoFonts s_infoFontsApplication;

QString PDFToolInfoFonts::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "info-fonts";

        case Name:
            return PDFToolTranslationContext::tr("Info about used fonts");

        case Description:
            return PDFToolTranslationContext::tr("Retrieve informations about font usage in a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolInfoFonts::execute(const PDFToolOptions& options)
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

    // The one walk over the resources of the report (K7.24, R7-5): the fonts of the pages, of their forms,
    // of the appearance streams of their annotations, of the patterns and of the Type 3 fonts, each once
    const std::vector<pdf::PDFDocumentReportFont> fonts = pdf::PDFDocumentReport::getFonts(&document, pages, options.showCharacterMapsForEmbeddedFonts, nullptr);

    auto getTypeText = [](pdf::FontType fontType)
    {
        switch (fontType)
        {
            case pdf::FontType::Type0:
                return PDFToolTranslationContext::tr("Type 0 (CID)");
            case pdf::FontType::Type1:
                return PDFToolTranslationContext::tr("Type 1 (8 bit)");
            case pdf::FontType::MMType1:
                return PDFToolTranslationContext::tr("MM Type 1 (8 bit)");
            case pdf::FontType::TrueType:
                return PDFToolTranslationContext::tr("TrueType (8 bit)");
            case pdf::FontType::Type3:
                return PDFToolTranslationContext::tr("Type 3");
            default:
                break;
        }
        return QString();
    };

    // The name without the tag of a subset ("ABCDEF+Name", 9.9.2), the full name in the character maps
    auto getShortName = [](const pdf::PDFDocumentReportFont& font)
    {
        return font.isSubset ? font.name.mid(7) : font.name;
    };

    auto getPages = [](const pdf::PDFDocumentReportFont& font)
    {
        pdf::PDFClosedIntervalSet pageSet;
        for (const pdf::PDFInteger pageIndex : font.pageIndices)
        {
            pageSet.addValue(pageIndex + 1);
        }
        return pageSet.toText(true);
    };

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("info-fonts", PDFToolTranslationContext::tr("Fonts used in document %1").arg(options.document));
    formatter.endl();

    formatter.beginTable("fonts-overview", PDFToolTranslationContext::tr("Overview"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("font-name", PDFToolTranslationContext::tr("Font Name"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("font-type", PDFToolTranslationContext::tr("Font Type"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("encoding", PDFToolTranslationContext::tr("Encoding"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("pages", PDFToolTranslationContext::tr("Pages"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("is-embedded", PDFToolTranslationContext::tr("Embedded"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("is-subset", PDFToolTranslationContext::tr("Subset"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("is-unicode", PDFToolTranslationContext::tr("Unicode"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("object-no", PDFToolTranslationContext::tr("Object"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("generation-no", PDFToolTranslationContext::tr("Gen."), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    // Q7.14: the numbers of PdfTool in the C locale
    const QLocale locale = QLocale::c();

    QString yesText = PDFToolTranslationContext::tr("Yes");
    QString noText = PDFToolTranslationContext::tr("No");
    QString noRef = PDFToolTranslationContext::tr("--");

    bool hasEmbedded = false;
    bool hasSubstitutions = false;
    int ref = 1;
    for (const pdf::PDFDocumentReportFont& info : fonts)
    {
        formatter.beginTableRow("font", ref);

        formatter.writeTableColumn("no", locale.toString(ref), Qt::AlignRight);
        formatter.writeTableColumn("font-name", getShortName(info));
        formatter.writeTableColumn("font-type", getTypeText(info.fontType));
        formatter.writeTableColumn("encoding", info.encoding);
        formatter.writeTableColumn("pages", getPages(info));
        formatter.writeTableColumn("is-embedded", info.isEmbedded ? yesText : noText);
        formatter.writeTableColumn("is-subset", info.isSubset ? yesText : noText);
        formatter.writeTableColumn("is-unicode", info.hasToUnicode ? yesText : noText);

        if (info.reference.isValid())
        {
            formatter.writeTableColumn("object-no", locale.toString(info.reference.objectNumber), Qt::AlignRight);
            formatter.writeTableColumn("generation-no", locale.toString(info.reference.generation), Qt::AlignRight);
        }
        else
        {
            formatter.writeTableColumn("object-no", noRef, Qt::AlignRight);
            formatter.writeTableColumn("generation-no", noRef, Qt::AlignRight);
        }

        hasSubstitutions = hasSubstitutions || !info.isEmbedded;
        hasEmbedded = hasEmbedded || info.isEmbedded;

        formatter.endTableRow();
        ++ref;
    }

    formatter.endTable();

    if (hasSubstitutions)
    {
        formatter.endl();
        formatter.beginTable("fonts-substitutions", PDFToolTranslationContext::tr("Substitutions"));

        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("font-name", PDFToolTranslationContext::tr("Font Name"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("font-substitute", PDFToolTranslationContext::tr("Substituted by Font"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("match", PDFToolTranslationContext::tr("Match"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("object-no", PDFToolTranslationContext::tr("Object"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("generation-no", PDFToolTranslationContext::tr("Gen."), Qt::AlignLeft);
        formatter.endTableHeaderRow();

        ref = 1;
        for (const pdf::PDFDocumentReportFont& info : fonts)
        {
            if (info.isEmbedded)
            {
                continue;
            }

            const QString fontName = getShortName(info);
            formatter.beginTableRow("substitution", ref);

            formatter.writeTableColumn("no", locale.toString(ref), Qt::AlignRight);
            formatter.writeTableColumn("font-name", fontName);
            formatter.writeTableColumn("font-substitute", !info.substitute.isEmpty() ? info.substitute : PDFToolTranslationContext::tr("??"));
            formatter.writeTableColumn("match", (fontName == info.substitute) ? yesText : noText);

            if (info.reference.isValid())
            {
                formatter.writeTableColumn("object-no", locale.toString(info.reference.objectNumber), Qt::AlignRight);
                formatter.writeTableColumn("generation-no", locale.toString(info.reference.generation), Qt::AlignRight);
            }
            else
            {
                formatter.writeTableColumn("object-no", noRef, Qt::AlignRight);
                formatter.writeTableColumn("generation-no", noRef, Qt::AlignRight);
            }

            formatter.endTableRow();
            ++ref;
        }

        formatter.endTable();
    }

    if (options.showCharacterMapsForEmbeddedFonts && hasEmbedded)
    {
        formatter.endl();
        formatter.beginHeader("font-character-maps", PDFToolTranslationContext::tr("Font Character Maps"));

        for (const pdf::PDFDocumentReportFont& info : fonts)
        {
            if (!info.isEmbedded)
            {
                continue;
            }

            formatter.beginTable("font-character-map", PDFToolTranslationContext::tr("Character Map for Font '%1'").arg(info.name));

            formatter.beginTableHeaderRow("header");
            formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
            formatter.writeTableHeaderColumn("glyph-index", PDFToolTranslationContext::tr("Glyph Index"), Qt::AlignLeft);
            formatter.writeTableHeaderColumn("character", PDFToolTranslationContext::tr("Character"), Qt::AlignLeft);
            formatter.writeTableHeaderColumn("unicode", PDFToolTranslationContext::tr("Unicode"), Qt::AlignLeft);
            formatter.endTableHeaderRow();

            int characterIndex = 1;
            for (const pdf::CharacterInfo& characterInfo : info.characterInfos)
            {
                formatter.beginTableRow("character", characterInfo.gid);

                QString character = characterInfo.character.isNull() ? "??" : QString(1, characterInfo.character);
                QString unicode = QString("0x%1").arg(QString::number(characterInfo.character.unicode(), 16).toUpper().rightJustified(4, QChar('0')));

                formatter.writeTableColumn("no", locale.toString(characterIndex++), Qt::AlignRight);
                formatter.writeTableColumn("glyph-index", locale.toString(characterInfo.gid), Qt::AlignRight);
                formatter.writeTableColumn("character", character);
                formatter.writeTableColumn("unicode", unicode);

                formatter.endTableRow();
            }

            formatter.endTable();
            formatter.endl();
        }

        formatter.endHeader();
    }

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInfoFonts::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | CharacterMaps;
}

}   // namespace pdftool
