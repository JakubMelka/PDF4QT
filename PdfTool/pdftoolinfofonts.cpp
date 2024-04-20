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

#include "pdftoolinfofonts.h"
#include "pdfexecutionpolicy.h"
#include "pdfdocument.h"
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

struct FontInfo
{
    pdf::PDFClosedIntervalSet pages;
    QString fontFullName;
    QString fontName;
    QString fontTypeName;
    QString encoding;
    bool isEmbedded = false;
    bool isSubset = false;
    bool isToUnicodePresent = false;
    pdf::PDFObjectReference reference;
    QString substitutedFont;
    pdf::CharacterInfos characterInfos;
};

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

    QMutex mutex;
    std::map<pdf::PDFObjectReference, FontInfo> fontInfoMap;
    std::vector<FontInfo> directFonts;
    std::set<pdf::PDFObjectReference> usedFontReferences;

    auto processPage = [&](pdf::PDFInteger pageIndex)
    {
        try
        {
            const pdf::PDFPage* page = document.getCatalog()->getPage(pageIndex);
            if (const pdf::PDFDictionary* resourcesDictionary = document.getDictionaryFromObject(page->getResources()))
            {
                if (const pdf::PDFDictionary* fontsDictionary = document.getDictionaryFromObject(resourcesDictionary->get("Font")))
                {
                    // Iterate trough each font
                    const size_t fontsCount = fontsDictionary->getCount();
                    for (size_t i = 0; i < fontsCount; ++i)
                    {
                        pdf::PDFObjectReference fontReference;
                        pdf::PDFObject object = fontsDictionary->getValue(i);
                        if (object.isReference())
                        {
                            // Check, if we have not processed the object. If we have it processed,
                            // then do nothing, otherwise insert it into the processed objects.
                            // We must also use mutex, because we use multithreading.
                            QMutexLocker lock(&mutex);
                            if (usedFontReferences.count(object.getReference()))
                            {
                                fontInfoMap[object.getReference()].pages.addValue(pageIndex + 1);
                                continue;
                            }
                            else
                            {
                                fontReference = object.getReference();
                                usedFontReferences.insert(fontReference);
                            }
                        }

                        try
                        {
                            if (pdf::PDFFontPointer font = pdf::PDFFont::createFont(object, fontsDictionary->getKey(i).getString(), &document))
                            {
                                pdf::PDFRenderErrorReporterDummy dummyReporter;
                                pdf::PDFRealizedFontPointer realizedFont = pdf::PDFRealizedFont::createRealizedFont(font, 8.0, &dummyReporter);
                                if (realizedFont)
                                {
                                    const pdf::FontType fontType = font->getFontType();
                                    const pdf::FontDescriptor* fontDescriptor = font->getFontDescriptor();
                                    QString fontName = fontDescriptor->fontName;
                                    QString fontFullName = fontName;
                                    int plusPos = fontName.lastIndexOf('+');

                                    // Jakub Melka: Detect, if font is subset. Font subsets have special form,
                                    // according to chapter 9.9.2 of PDF 2.0 specification. The first 6 letters
                                    // of font name are uppercase alphabet letters, and 7'th character is '+' sign.
                                    bool isSubset = false;
                                    if (plusPos == 6)
                                    {
                                        isSubset = true;
                                        for (int iFontName = 0; iFontName < 6; ++iFontName)
                                        {
                                            QChar character = fontName[iFontName];
                                            if (!character.isLetter() || !character.isUpper())
                                            {
                                                isSubset = false;
                                                break;
                                            }
                                        }
                                    }

                                    // Try to remove characters from +, if we have font name 'SDFDSF+ValidFontName'
                                    if (plusPos != -1 && plusPos < fontName.size() - 1)
                                    {
                                        fontName = fontName.mid(plusPos + 1);
                                    }

                                    if (fontName.isEmpty())
                                    {
                                        fontName = QString::fromLatin1(fontsDictionary->getKey(i).getString());
                                    }

                                    QString fontTypeName;
                                    switch (fontType)
                                    {
                                        case pdf::FontType::Type0:
                                            fontTypeName = PDFToolTranslationContext::tr("Type 0 (CID)");
                                            break;

                                        case pdf::FontType::Type1:
                                            fontTypeName = PDFToolTranslationContext::tr("Type 1 (8 bit)");
                                            break;

                                        case pdf::FontType::MMType1:
                                            fontTypeName = PDFToolTranslationContext::tr("MM Type 1 (8 bit)");
                                            break;

                                        case pdf::FontType::TrueType:
                                            fontTypeName = PDFToolTranslationContext::tr("TrueType (8 bit)");
                                            break;

                                        case pdf::FontType::Type3:
                                            fontTypeName = PDFToolTranslationContext::tr("Type 3");
                                            break;

                                        default:
                                            Q_ASSERT(false);
                                            break;
                                    }

                                    const pdf::PDFFontCMap* toUnicode = font->getToUnicode();

                                    FontInfo info;
                                    info.fontName = fontName;
                                    info.fontFullName = fontFullName;
                                    info.pages.addValue(pageIndex + 1);
                                    info.fontTypeName = fontTypeName;
                                    info.isEmbedded = fontDescriptor->isEmbedded() || fontType == pdf::FontType::Type3;
                                    info.isSubset = isSubset;
                                    info.isToUnicodePresent = toUnicode && toUnicode->isValid();
                                    info.reference = fontReference;
                                    info.substitutedFont = realizedFont->getPostScriptName();

                                    if (options.showCharacterMapsForEmbeddedFonts && info.isEmbedded)
                                    {
                                        info.characterInfos = realizedFont->getCharacterInfos();
                                    }

                                    const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(font.data());
                                    if (simpleFont)
                                    {
                                        const pdf::PDFEncoding::Encoding encoding = simpleFont->getEncodingType();
                                        switch (encoding)
                                        {
                                            case pdf::PDFEncoding::Encoding::Standard:
                                                info.encoding = PDFToolTranslationContext::tr("Standard");
                                                break;
                                            case pdf::PDFEncoding::Encoding::MacRoman:
                                                info.encoding = PDFToolTranslationContext::tr("MacRoman");
                                                break;
                                            case pdf::PDFEncoding::Encoding::WinAnsi:
                                                info.encoding = PDFToolTranslationContext::tr("WinAnsi");
                                                break;
                                            case pdf::PDFEncoding::Encoding::PDFDoc:
                                                info.encoding = PDFToolTranslationContext::tr("PDFDoc");
                                                break;
                                            case pdf::PDFEncoding::Encoding::MacExpert:
                                                info.encoding = PDFToolTranslationContext::tr("MacExpert");
                                                break;
                                            case pdf::PDFEncoding::Encoding::Symbol:
                                                info.encoding = PDFToolTranslationContext::tr("Symbol");
                                                break;
                                            case pdf::PDFEncoding::Encoding::ZapfDingbats:
                                                info.encoding = PDFToolTranslationContext::tr("ZapfDingbats");
                                                break;

                                            default:
                                                info.encoding = PDFToolTranslationContext::tr("Custom");
                                                break;
                                        }
                                    }

                                    QMutexLocker lock(&mutex);
                                    if (fontReference.isValid())
                                    {
                                        info.pages.merge(fontInfoMap[fontReference].pages);
                                        fontInfoMap[fontReference] = qMove(info);
                                    }
                                    else
                                    {
                                        directFonts.emplace_back(qMove(info));
                                    }
                                }
                            }
                        }
                        catch (const pdf::PDFException &)
                        {
                            // Do nothing, some error occured, continue with next font
                            continue;
                        }
                    }
                }
            }
        }
        catch (const pdf::PDFException &)
        {
            // Do nothing, some error occured
        }
    };

    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Page, pages.begin(), pages.end(), processPage);

    for (auto& item : fontInfoMap)
    {
        directFonts.emplace_back(qMove(item.second));
    }

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

    QLocale locale;

    QString yesText = PDFToolTranslationContext::tr("Yes");
    QString noText = PDFToolTranslationContext::tr("No");
    QString noRef = PDFToolTranslationContext::tr("--");

    bool hasEmbedded = false;
    bool hasSubstitutions = false;
    int ref = 1;
    for (const FontInfo& info : directFonts)
    {
        formatter.beginTableRow("font", ref);

        formatter.writeTableColumn("no", locale.toString(ref), Qt::AlignRight);
        formatter.writeTableColumn("font-name", info.fontName);
        formatter.writeTableColumn("font-type", info.fontTypeName);
        formatter.writeTableColumn("encoding", info.encoding);
        formatter.writeTableColumn("pages", info.pages.toText(true));
        formatter.writeTableColumn("is-embedded", info.isEmbedded ? yesText : noText);
        formatter.writeTableColumn("is-subset", info.isSubset ? yesText : noText);
        formatter.writeTableColumn("is-unicode", info.isToUnicodePresent ? yesText : noText);

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
        for (const FontInfo& info : directFonts)
        {
            if (info.isEmbedded)
            {
                continue;
            }

            formatter.beginTableRow("substitution", ref);

            formatter.writeTableColumn("no", locale.toString(ref), Qt::AlignRight);
            formatter.writeTableColumn("font-name", info.fontName);
            formatter.writeTableColumn("font-substitute", !info.substitutedFont.isEmpty() ? info.substitutedFont : PDFToolTranslationContext::tr("??"));
            formatter.writeTableColumn("match", (info.fontName == info.substitutedFont) ? yesText : noText);

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

        int fontRef = 1;
        for (const FontInfo& info : directFonts)
        {
            if (!info.isEmbedded)
            {
                continue;
            }

            formatter.beginTable("font-character-map", PDFToolTranslationContext::tr("Character Map for Font '%1'").arg(info.fontFullName));

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
            ++fontRef;

            formatter.endl();
        }

        formatter.endTable();
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
