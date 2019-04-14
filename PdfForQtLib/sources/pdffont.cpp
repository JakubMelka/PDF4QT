//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdffont.h"
#include "pdfdocument.h"
#include "pdfparser.h"
#include "pdfnametounicode.h"

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <freetype/fterrors.h>
#include <freetype/ftoutln.h>

#include <QMutex>

#ifdef Q_OS_WIN
#include "Windows.h"

#pragma comment(lib, "Gdi32")
#pragma comment(lib, "User32")
#endif

namespace pdf
{

/// Storage class for system fonts
class PDFSystemFontInfoStorage
{
public:

    /// Returns instance of storage
    static const PDFSystemFontInfoStorage* getInstance();

    /// Loads font from descriptor
    /// \param descriptor Descriptor describing the font
    QByteArray loadFont(const FontDescriptor* descriptor) const;

private:
    explicit PDFSystemFontInfoStorage();

#ifdef Q_OS_WIN
    /// Callback for enumerating fonts
    static int CALLBACK enumerateFontProc(const LOGFONT* font, const TEXTMETRIC* textMetrics, DWORD fontType, LPARAM lParam);

    /// Retrieves font data for desired font
    static QByteArray getFontData(const LOGFONT* font, HDC hdc);

    struct FontInfo
    {
        QString faceName;
        LOGFONT logFont;
        TEXTMETRIC textMetric;
    };

    struct CallbackInfo
    {
        PDFSystemFontInfoStorage* storage = nullptr;
        HDC hdc = nullptr;
    };

    std::vector<FontInfo> m_fontInfos;
#endif
};

const PDFSystemFontInfoStorage* PDFSystemFontInfoStorage::getInstance()
{
    static PDFSystemFontInfoStorage instance;
    return &instance;
}

QByteArray PDFSystemFontInfoStorage::loadFont(const FontDescriptor* descriptor) const
{
    QByteArray result;

#ifdef Q_OS_WIN
    HDC hdc = GetDC(NULL);

    // Exact match for font, if font can't be exact matched, then match font family
    // and try to set weight
    QString fontFamily = QString::fromLatin1(descriptor->fontFamily);
    for (const FontInfo& fontInfo : m_fontInfos)
    {
        if (fontInfo.faceName.contains(fontFamily) &&
            fontInfo.logFont.lfWeight == descriptor->fontWeight &&
            fontInfo.logFont.lfItalic == (descriptor->italicAngle != 0.0 ? TRUE : FALSE))
        {
            result = getFontData(&fontInfo.logFont, hdc);

            if (!result.isEmpty())
            {
                break;
            }
        }
    }

    // Match for font family
    if (result.isEmpty())
    {
        for (const FontInfo& fontInfo : m_fontInfos)
        {
            if (fontInfo.faceName.contains(fontFamily))
            {
                LOGFONT logFont = fontInfo.logFont;
                logFont.lfWeight = descriptor->fontWeight;
                logFont.lfItalic = (descriptor->italicAngle != 0.0 ? TRUE : FALSE);
                result = getFontData(&logFont, hdc);

                if (!result.isEmpty())
                {
                    break;
                }
            }
        }
    }

    ReleaseDC(NULL, hdc);
#endif

    return result;
}

PDFSystemFontInfoStorage::PDFSystemFontInfoStorage()
{
#ifdef Q_OS_WIN
    LOGFONT logfont;
    std::memset(&logfont, 0, sizeof(logfont));
    logfont.lfCharSet = DEFAULT_CHARSET;
    logfont.lfFaceName[0] = 0;
    logfont.lfPitchAndFamily = 0;

    HDC hdc = GetDC(NULL);

    CallbackInfo callbackInfo{ this, hdc};
    EnumFontFamiliesEx(hdc, &logfont, &PDFSystemFontInfoStorage::enumerateFontProc, reinterpret_cast<LPARAM>(&callbackInfo), 0);

    ReleaseDC(NULL, hdc);
#endif
}

#ifdef Q_OS_WIN
int PDFSystemFontInfoStorage::enumerateFontProc(const LOGFONT* font, const TEXTMETRIC* textMetrics, DWORD fontType, LPARAM lParam)
{
    if ((fontType & TRUETYPE_FONTTYPE) && (font->lfCharSet == ANSI_CHARSET))
    {
        CallbackInfo* callbackInfo = reinterpret_cast<CallbackInfo*>(lParam);

        FontInfo fontInfo;
        fontInfo.logFont = *font;
        fontInfo.textMetric = *textMetrics;
        fontInfo.faceName = QString::fromWCharArray(font->lfFaceName);
        callbackInfo->storage->m_fontInfos.push_back(qMove(fontInfo));

        // For debug purposes only!
#if 0
        QByteArray byteArray = getFontData(font, callbackInfo->hdc);
        qDebug() << "Font: " << QString::fromWCharArray(font->lfFaceName) << ", italic = " << font->lfItalic << ", weight = " << font->lfWeight << ", data size = " << byteArray.size();
#endif
    }

    return TRUE;
}

QByteArray PDFSystemFontInfoStorage::getFontData(const LOGFONT* font, HDC hdc)
{
    QByteArray byteArray;

    if (HFONT fontHandle = ::CreateFontIndirect(font))
    {
        HGDIOBJ oldFont = ::SelectObject(hdc, fontHandle);

        DWORD size = ::GetFontData(hdc, 0, 0, nullptr, 0);
        if (size != GDI_ERROR)
        {
            byteArray.resize(static_cast<int>(size));
            ::GetFontData(hdc, 0, 0, byteArray.data(), byteArray.size());
        }

        ::SelectObject(hdc, oldFont);
        ::DeleteObject(fontHandle);
    }

    return byteArray;
}
#endif

PDFFont::PDFFont(FontDescriptor fontDescriptor) :
    m_fontDescriptor(qMove(fontDescriptor))
{

}

/// Implementation of the PDFRealizedFont class using PIMPL pattern
class PDFRealizedFontImpl
{
public:
    explicit PDFRealizedFontImpl();
    ~PDFRealizedFontImpl();

    /// Fills the text sequence by interpreting byte array according font data and
    /// produces glyphs for the font.
    /// \param byteArray Array of bytes to be interpreted
    /// \param textSequence Text sequence to be filled
    void fillTextSequence(const QByteArray& byteArray, TextSequence& textSequence);

private:
    friend class PDFRealizedFont;

    static constexpr const PDFReal FORMAT_26_6_MULTIPLIER = 1 / 64.0;

    struct Glyph
    {
        QPainterPath glyph;
        PDFReal advance;
    };

    static int outlineMoveTo(const FT_Vector* to, void* user);
    static int outlineLineTo(const FT_Vector* to, void* user);
    static int outlineConicTo(const FT_Vector* control, const FT_Vector* to, void* user);
    static int outlineCubicTo(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user);

    /// Get glyph for unicode character. Throws exception, if glyph can't be found.
    const Glyph& getGlyphForUnicode(QChar character);

    /// Function checks, if error occured, and if yes, then exception is thrown
    static void checkFreeTypeError(FT_Error error);

    /// Mutex for accessing the glyph data
    QMutex m_mutex;

    /// Glyph cache, must be protected by the mutex above
    std::map<QChar, Glyph> m_glyphCache;

    /// For embedded fonts, this byte array contains embedded font data
    QByteArray m_embeddedFontData;

    /// For system fonts, this byte array contains system font data
    QByteArray m_systemFontData;

    /// Instance of FreeType library assigned to this font
    FT_Library m_library;

    /// Face of the font
    FT_Face m_face;

    /// Pixel size of the font
    PDFReal m_pixelSize;

    /// Parent font
    PDFFontPointer m_parentFont;

    /// True, if font is embedded
    bool m_isEmbedded;

    /// True, if font has vertical writing system
    bool m_isVertical;
};

PDFRealizedFontImpl::PDFRealizedFontImpl() :
    m_library(nullptr),
    m_face(nullptr),
    m_pixelSize(0.0),
    m_parentFont(nullptr),
    m_isEmbedded(false),
    m_isVertical(false)
{

}

PDFRealizedFontImpl::~PDFRealizedFontImpl()
{
    if (m_face)
    {
        FT_Done_Face(m_face);
        m_face = nullptr;
    }

    if (m_library)
    {
        FT_Done_FreeType(m_library);
        m_library = nullptr;
    }
}

void PDFRealizedFontImpl::fillTextSequence(const QByteArray& byteArray, TextSequence& textSequence)
{
    switch (m_parentFont->getFontType())
    {
        case FontType::Type1:
        case FontType::TrueType:
        {
            // We can use encoding
            QString text = m_parentFont->getTextUsingEncoding(byteArray);

            textSequence.items.reserve(textSequence.items.size() + text.size());
            for (const QChar& character : text)
            {
                const Glyph& glyph = getGlyphForUnicode(character);
                textSequence.items.emplace_back(&glyph.glyph, character, glyph.advance);
            }
            break;
        }

        default:
        {
            // Unhandled font type
            Q_ASSERT(false);
            break;
        }
    }
}

int PDFRealizedFontImpl::outlineMoveTo(const FT_Vector* to, void* user)
{
    Glyph* glyph = reinterpret_cast<Glyph*>(user);
    glyph->glyph.moveTo(to->x * FORMAT_26_6_MULTIPLIER, to->y * FORMAT_26_6_MULTIPLIER);
    return 0;
}

int PDFRealizedFontImpl::outlineLineTo(const FT_Vector* to, void* user)
{
    Glyph* glyph = reinterpret_cast<Glyph*>(user);
    glyph->glyph.lineTo(to->x * FORMAT_26_6_MULTIPLIER, to->y * FORMAT_26_6_MULTIPLIER);
    return 0;
}

int PDFRealizedFontImpl::outlineConicTo(const FT_Vector* control, const FT_Vector* to, void* user)
{
    Glyph* glyph = reinterpret_cast<Glyph*>(user);
    glyph->glyph.cubicTo(control->x * FORMAT_26_6_MULTIPLIER, control->y * FORMAT_26_6_MULTIPLIER, control->x * FORMAT_26_6_MULTIPLIER, control->y * FORMAT_26_6_MULTIPLIER, to->x * FORMAT_26_6_MULTIPLIER, to->y * FORMAT_26_6_MULTIPLIER);
    return 0;
}

int PDFRealizedFontImpl::outlineCubicTo(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user)
{
    Glyph* glyph = reinterpret_cast<Glyph*>(user);
    glyph->glyph.cubicTo(control1->x * FORMAT_26_6_MULTIPLIER, control1->y * FORMAT_26_6_MULTIPLIER, control2->x * FORMAT_26_6_MULTIPLIER, control2->y * FORMAT_26_6_MULTIPLIER, to->x * FORMAT_26_6_MULTIPLIER, to->y * FORMAT_26_6_MULTIPLIER);
    return 0;
}

const PDFRealizedFontImpl::Glyph& PDFRealizedFontImpl::getGlyphForUnicode(QChar character)
{
    QMutexLocker lock(&m_mutex);

    // First look into cache
    auto it = m_glyphCache.find(character);
    if (it != m_glyphCache.cend())
    {
        return it->second;
    }

    FT_UInt glyphIndex = FT_Get_Char_Index(m_face, character.unicode());
    if (glyphIndex)
    {
        Glyph glyph;

        FT_Outline_Funcs glyphOutlineInterface;
        glyphOutlineInterface.delta = 0;
        glyphOutlineInterface.shift = 0;
        glyphOutlineInterface.move_to = PDFRealizedFontImpl::outlineMoveTo;
        glyphOutlineInterface.line_to = PDFRealizedFontImpl::outlineLineTo;
        glyphOutlineInterface.conic_to = PDFRealizedFontImpl::outlineConicTo;
        glyphOutlineInterface.cubic_to = PDFRealizedFontImpl::outlineCubicTo;

        checkFreeTypeError(FT_Load_Glyph(m_face, glyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING));
        checkFreeTypeError(FT_Outline_Decompose(&m_face->glyph->outline, &glyphOutlineInterface, &glyph));
        glyph.glyph.closeSubpath();
        glyph.advance = !m_isVertical ? m_face->glyph->advance.x : m_face->glyph->advance.y;
        glyph.advance *= FORMAT_26_6_MULTIPLIER;

        m_glyphCache[character] = qMove(glyph);
        return m_glyphCache[character];
    }
    else
    {
        throw PDFParserException(PDFTranslationContext::tr("Glyph for unicode character '%1' not found.").arg(character));
    }

    static Glyph dummy;
    return dummy;
}

void PDFRealizedFontImpl::checkFreeTypeError(FT_Error error)
{
    if (error)
    {
        QString message;
        if (const char* errorString = FT_Error_String(error))
        {
            message = QString::fromLatin1(errorString);
        }

        throw PDFParserException(PDFTranslationContext::tr("FreeType error code %1: message").arg(error).arg(message));
    }
}

PDFRealizedFont::~PDFRealizedFont()
{
    delete m_impl;
}

void PDFRealizedFont::fillTextSequence(const QByteArray& byteArray, TextSequence& textSequence)
{
    m_impl->fillTextSequence(byteArray, textSequence);
}

bool PDFRealizedFont::isHorizontalWritingSystem() const
{
    return !m_impl->m_isVertical;
}

PDFRealizedFontPointer PDFRealizedFont::createRealizedFont(PDFFontPointer font, PDFReal pixelSize)
{
    PDFRealizedFontPointer result;
    std::unique_ptr<PDFRealizedFontImpl> implPtr(new PDFRealizedFontImpl());

    PDFRealizedFontImpl* impl = implPtr.get();
    impl->m_parentFont = font;

    const FontDescriptor* descriptor = font->getFontDescriptor();
    if (descriptor->isEmbedded())
    {

        PDFRealizedFontImpl::checkFreeTypeError(FT_Init_FreeType(&impl->m_library));

        if (!descriptor->fontFile.isEmpty())
        {
            impl->m_embeddedFontData = descriptor->fontFile;
        }
        else if (!descriptor->fontFile2.isEmpty())
        {
            impl->m_embeddedFontData = descriptor->fontFile2;
        }
        else if (!descriptor->fontFile3.isEmpty())
        {
            impl->m_embeddedFontData = descriptor->fontFile3;
        }

        // At this time, embedded font data should not be empty!
        Q_ASSERT(!impl->m_embeddedFontData.isEmpty());

        PDFRealizedFontImpl::checkFreeTypeError(FT_New_Memory_Face(impl->m_library, reinterpret_cast<const FT_Byte*>(impl->m_embeddedFontData.constData()), impl->m_embeddedFontData.size(), 0, &impl->m_face));
        PDFRealizedFontImpl::checkFreeTypeError(FT_Select_Charmap(impl->m_face, FT_ENCODING_UNICODE));
        PDFRealizedFontImpl::checkFreeTypeError(FT_Set_Pixel_Sizes(impl->m_face, 0, qRound(pixelSize)));
        impl->m_isVertical = impl->m_face->face_flags & FT_FACE_FLAG_VERTICAL;
        impl->m_isEmbedded = true;
        result.reset(new PDFRealizedFont(implPtr.release()));
    }
    else
    {
        const PDFSystemFontInfoStorage* fontStorage = PDFSystemFontInfoStorage::getInstance();
        impl->m_systemFontData = fontStorage->loadFont(descriptor);

        if (impl->m_systemFontData.isEmpty())
        {
            // TODO: Upravit vyjimky do separatniho souboru
            throw PDFParserException(PDFTranslationContext::tr("Can't load system font '%1'.").arg(QString::fromLatin1(descriptor->fontName)));
        }

        PDFRealizedFontImpl::checkFreeTypeError(FT_Init_FreeType(&impl->m_library));
        PDFRealizedFontImpl::checkFreeTypeError(FT_New_Memory_Face(impl->m_library, reinterpret_cast<const FT_Byte*>(impl->m_systemFontData.constData()), impl->m_systemFontData.size(), 0, &impl->m_face));
        PDFRealizedFontImpl::checkFreeTypeError(FT_Select_Charmap(impl->m_face, FT_ENCODING_UNICODE));
        PDFRealizedFontImpl::checkFreeTypeError(FT_Set_Pixel_Sizes(impl->m_face, 0, qRound(pixelSize)));
        impl->m_isVertical = impl->m_face->face_flags & FT_FACE_FLAG_VERTICAL;
        impl->m_isEmbedded = false;
        result.reset(new PDFRealizedFont(implPtr.release()));
    }

    return result;
}

PDFFontPointer PDFFont::createFont(const PDFObject& object, const PDFDocument* document)
{
    // TODO: Create font cache for realized fonts
    const PDFObject& dereferencedFontDictionary = document->getObject(object);
    if (!dereferencedFontDictionary.isDictionary())
    {
        throw PDFParserException(PDFTranslationContext::tr("Font object must be a dictionary."));
    }

    const PDFDictionary* fontDictionary = dereferencedFontDictionary.getDictionary();
    PDFDocumentDataLoaderDecorator fontLoader(document);

    // TODO: Fonts - implement all types of the font
    // First, determine the font subtype
    constexpr const std::array<std::pair<const char*, FontType>, 2> fontTypes = {
        std::pair<const char*, FontType>{ "Type1", FontType::Type1 },
        std::pair<const char*, FontType>{ "TrueType", FontType::TrueType }
    };

    const FontType fontType = fontLoader.readEnumByName(fontDictionary->get("Subtype"), fontTypes.cbegin(), fontTypes.cend(), FontType::Invalid);
    if (fontType == FontType::Invalid)
    {
        throw PDFParserException(PDFTranslationContext::tr("Invalid font type."));
    }

    QByteArray name = fontLoader.readNameFromDictionary(fontDictionary, "Name");
    QByteArray baseFont = fontLoader.readNameFromDictionary(fontDictionary, "BaseFont");
    const PDFInteger firstChar = fontLoader.readIntegerFromDictionary(fontDictionary, "FirstChar", 0);
    const PDFInteger lastChar = fontLoader.readIntegerFromDictionary(fontDictionary, "LastChar", 255);
    std::vector<PDFInteger> widths = fontLoader.readIntegerArrayFromDictionary(fontDictionary, "Widths");

    // Read standard font
    constexpr const std::array<std::pair<const char*, StandardFontType>, 14> standardFonts = {
        std::pair<const char*, StandardFontType>{ "Times-Roman", StandardFontType::TimesRoman },
        std::pair<const char*, StandardFontType>{ "Times-Bold", StandardFontType::TimesRomanBold },
        std::pair<const char*, StandardFontType>{ "Times-Italic", StandardFontType::TimesRomanItalics },
        std::pair<const char*, StandardFontType>{ "Times-BoldItalic", StandardFontType::TimesRomanBoldItalics },
        std::pair<const char*, StandardFontType>{ "Helvetica", StandardFontType::Helvetica },
        std::pair<const char*, StandardFontType>{ "Helvetica-Bold", StandardFontType::HelveticaBold },
        std::pair<const char*, StandardFontType>{ "Helvetica-Oblique", StandardFontType::HelveticaOblique },
        std::pair<const char*, StandardFontType>{ "Helvetica-BoldOblique", StandardFontType::HelveticaBoldOblique },
        std::pair<const char*, StandardFontType>{ "Courier", StandardFontType::Courier },
        std::pair<const char*, StandardFontType>{ "Courier-Bold", StandardFontType::CourierBold },
        std::pair<const char*, StandardFontType>{ "Courier-Oblique", StandardFontType::CourierOblique },
        std::pair<const char*, StandardFontType>{ "Courier-BoldOblique", StandardFontType::CourierBoldOblique },
        std::pair<const char*, StandardFontType>{ "Symbol", StandardFontType::Symbol },
        std::pair<const char*, StandardFontType>{ "ZapfDingbats", StandardFontType::ZapfDingbats }
    };
    const StandardFontType standardFont = fontLoader.readEnumByName(fontDictionary->get("BaseFont"), standardFonts.cbegin(), standardFonts.cend(), StandardFontType::Invalid);

    // Read Font Descriptor
    FontDescriptor fontDescriptor;
    const PDFObject& fontDescriptorObject = document->getObject(fontDictionary->get("FontDescriptor"));
    if (fontDescriptorObject.isDictionary())
    {
        const PDFDictionary* fontDescriptorDictionary = fontDescriptorObject.getDictionary();
        fontDescriptor.fontName = fontLoader.readNameFromDictionary(fontDescriptorDictionary, "FontName");
        fontDescriptor.fontFamily = fontLoader.readStringFromDictionary(fontDescriptorDictionary, "FontFamily");

        constexpr const std::array<std::pair<const char*, QFont::Stretch>, 9> stretches = {
            std::pair<const char*, QFont::Stretch>{ "UltraCondensed", QFont::UltraCondensed },
            std::pair<const char*, QFont::Stretch>{ "ExtraCondensed", QFont::ExtraCondensed },
            std::pair<const char*, QFont::Stretch>{ "Condensed", QFont::Condensed },
            std::pair<const char*, QFont::Stretch>{ "SemiCondensed", QFont::SemiCondensed },
            std::pair<const char*, QFont::Stretch>{ "Normal", QFont::Unstretched },
            std::pair<const char*, QFont::Stretch>{ "SemiExpanded", QFont::SemiExpanded },
            std::pair<const char*, QFont::Stretch>{ "Expanded", QFont::Expanded },
            std::pair<const char*, QFont::Stretch>{ "ExtraExpanded", QFont::ExtraExpanded },
            std::pair<const char*, QFont::Stretch>{ "UltraExpanded", QFont::UltraExpanded }
        };
        fontDescriptor.fontStretch = fontLoader.readEnumByName(fontDescriptorDictionary->get("FontStretch"), stretches.cbegin(), stretches.cend(), QFont::Unstretched);
        fontDescriptor.fontWeight = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "FontWeight", 500);
        fontDescriptor.italicAngle = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "ItalicAngle", 0.0);
        fontDescriptor.ascent = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "Ascent", 0.0);
        fontDescriptor.descent = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "Descent", 0.0);
        fontDescriptor.leading = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "Leading", 0.0);
        fontDescriptor.capHeight = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "CapHeight", 0.0);
        fontDescriptor.xHeight = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "XHeight", 0.0);
        fontDescriptor.stemV = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "StemV", 0.0);
        fontDescriptor.stemH = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "StemH", 0.0);
        fontDescriptor.avgWidth = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "AvgWidth", 0.0);
        fontDescriptor.maxWidth = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "MaxWidth", 0.0);
        fontDescriptor.missingWidth = fontLoader.readNumberFromDictionary(fontDescriptorDictionary, "MissingWidth", 0.0);
        fontDescriptor.flags = fontLoader.readIntegerFromDictionary(fontDescriptorDictionary, "Flags", 0);
        fontDescriptor.boundingBox = fontLoader.readRectangle(fontDescriptorDictionary->get("FontBBox"), QRectF());
        fontDescriptor.charset = fontLoader.readStringFromDictionary(fontDescriptorDictionary, "Charset");

        auto loadStream = [fontDescriptorDictionary, document](QByteArray& byteArray, const char* name)
        {
            if (fontDescriptorDictionary->hasKey(name))
            {
                const PDFObject& streamObject = document->getObject(fontDescriptorDictionary->get(name));
                if (streamObject.isStream())
                {
                    byteArray = document->getDecodedStream(streamObject.getStream());
                }
            }
        };
        loadStream(fontDescriptor.fontFile, "FontFile");
        loadStream(fontDescriptor.fontFile2, "FontFile2");
        loadStream(fontDescriptor.fontFile3, "FontFile3");
    }

    // Read Font Encoding
    // The font encoding for the simple font is determined by this algorithm:
    //      1) Try to use Encoding dictionary to determine base encoding
    //         (it can be MacRomanEncoding, MacExpertEncoding, WinAnsiEncoding or StandardEncoding)
    //      2) If it is not present, then try to obtain built-in encoding from the font file (usually, this is not possible)
    //      3) Use default encoding for the font depending on the font type
    //          - one of the 14 base fonts - use builtin encoding for the font type
    //          - TrueType - use WinAnsiEncoding
    //          - all others - use StandardEncoding
    //      4) Merge with Differences, if present
    //      5) Fill missing characters from StandardEncoding

    // TODO: Read font encoding from the font file
    PDFEncoding::Encoding encoding = PDFEncoding::Encoding::Invalid;
    encoding::EncodingTable simpleFontEncodingTable = { };
    switch (fontType)
    {
        case FontType::Type1:
        case FontType::TrueType:
        {
            bool hasDifferences = false;
            encoding::EncodingTable differences = { };

            if (fontDictionary->hasKey("Encoding"))
            {
                constexpr const std::array<std::pair<const char*, PDFEncoding::Encoding>, 3> encodings = {
                    std::pair<const char*, PDFEncoding::Encoding>{ "MacRomanEncoding", PDFEncoding::Encoding::MacRoman },
                    std::pair<const char*, PDFEncoding::Encoding>{ "MacExpertEncoding", PDFEncoding::Encoding::MacExpert },
                    std::pair<const char*, PDFEncoding::Encoding>{ "WinAnsiEncoding", PDFEncoding::Encoding::WinAnsi }
                };

                const PDFObject& encodingObject = document->getObject(fontDictionary->get("Encoding"));
                if (encodingObject.isName())
                {
                    // Decode name of the encoding
                    encoding = fontLoader.readEnumByName(encodingObject, encodings.cbegin(), encodings.cend(), PDFEncoding::Encoding::Invalid);
                }
                else if (encodingObject.isDictionary())
                {
                    // Dictionary with base encoding and differences (all optional)
                    const PDFDictionary* encodingDictionary = encodingObject.getDictionary();
                    if (encodingDictionary->hasKey("BaseEncoding"))
                    {
                        encoding = fontLoader.readEnumByName(encodingDictionary->get("BaseEncoding"), encodings.cbegin(), encodings.cend(), PDFEncoding::Encoding::Invalid);
                    }
                    else
                    {
                        // We get encoding for the standard font. If we have invalid standard font,
                        // then we get standard encoding. So we shouldn't test it.
                        encoding = getEncodingForStandardFont(standardFont);
                    }

                    if (encodingDictionary->hasKey("Differences"))
                    {
                        const PDFObject& differencesArray = document->getObject(encodingDictionary->get("Differences"));
                        if (differencesArray.isArray())
                        {
                            hasDifferences = true;
                            const PDFArray* array = differencesArray.getArray();
                            size_t currentOffset = 0;
                            for (size_t i = 0, count = array->getCount(); i < count; ++i)
                            {
                                const PDFObject& item = document->getObject(array->getItem(i));
                                if (item.isInt())
                                {
                                    currentOffset = static_cast<size_t>(item.getInteger());
                                }
                                else if (item.isName())
                                {
                                    if (currentOffset >= differences.size())
                                    {
                                        throw PDFParserException(PDFTranslationContext::tr("Invalid differences in encoding entry of the font."));
                                    }

                                    QChar character = PDFNameToUnicode::getUnicodeForName(item.getString());

                                    // Try ZapfDingbats, if this fails
                                    if (character.isNull())
                                    {
                                        character = PDFNameToUnicode::getUnicodeForNameZapfDingbats(item.getString());
                                    }
                                    differences[currentOffset] = character;

                                    ++currentOffset;
                                }
                                else
                                {
                                    throw PDFParserException(PDFTranslationContext::tr("Invalid differences in encoding entry of the font."));
                                }
                            }
                        }
                        else
                        {
                            throw PDFParserException(PDFTranslationContext::tr("Invalid differences in encoding entry of the font."));
                        }
                    }
                }
                else
                {
                    throw PDFParserException(PDFTranslationContext::tr("Invalid encoding entry of the font."));
                }
            }
            else
            {
                // We get encoding for the standard font. If we have invalid standard font,
                // then we get standard encoding. So we shouldn't test it.
                encoding = getEncodingForStandardFont(standardFont);
            }

            if (encoding == PDFEncoding::Encoding::Invalid)
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid encoding entry of the font."));
            }

            simpleFontEncodingTable = *PDFEncoding::getTableForEncoding(encoding);

            // Fill in differences
            if (hasDifferences)
            {
                for (size_t i = 0; i < differences.size(); ++i)
                {
                    if (!differences[i].isNull())
                    {
                        simpleFontEncodingTable[i] = differences[i];
                    }
                }

                // Set the encoding to custom
                encoding = PDFEncoding::Encoding::Custom;
            }

            // Fill in missing characters from standard encoding
            const encoding::EncodingTable& standardEncoding = *PDFEncoding::getTableForEncoding(PDFEncoding::Encoding::Standard);
            for (size_t i = 0; i < standardEncoding.size(); ++i)
            {
                if ((simpleFontEncodingTable[i].isNull() || simpleFontEncodingTable[i] == QChar(QChar::SpecialCharacter::ReplacementCharacter)) &&
                    (!standardEncoding[i].isNull() && standardEncoding[i] != QChar(QChar::SpecialCharacter::ReplacementCharacter)))
                {
                    simpleFontEncodingTable[i] = standardEncoding[i];
                }
            }

            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    switch (fontType)
    {
        case FontType::Type1:
            return PDFFontPointer(new PDFType1Font(qMove(fontDescriptor), qMove(name), qMove(baseFont), firstChar, lastChar, qMove(widths), encoding, simpleFontEncodingTable, standardFont));

        case FontType::TrueType:
            return PDFFontPointer(new PDFTrueTypeFont(qMove(fontDescriptor), qMove(name), qMove(baseFont), firstChar, lastChar, qMove(widths), encoding, simpleFontEncodingTable));

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    // Read To Unicode
    // TODO: Read To Unicode

    // Read Embedded fonts
    // TODO: Read embedded fonts
    return PDFFontPointer();
}

PDFSimpleFont::PDFSimpleFont(FontDescriptor fontDescriptor,
                             QByteArray name,
                             QByteArray baseFont,
                             PDFInteger firstChar,
                             PDFInteger lastChar,
                             std::vector<PDFInteger> widths,
                             PDFEncoding::Encoding encodingType,
                             encoding::EncodingTable encoding) :
    PDFFont(qMove(fontDescriptor)),
    m_name(qMove(name)),
    m_baseFont(qMove(baseFont)),
    m_firstChar(firstChar),
    m_lastChar(lastChar),
    m_widths(qMove(widths)),
    m_encodingType(encodingType),
    m_encoding(encoding)
{

}

PDFRealizedFontPointer PDFSimpleFont::getRealizedFont(PDFFontPointer font, PDFReal fontSize) const
{
    // TODO: Fix font creation to use also embedded fonts, font descriptor, etc.
    // TODO: Remove QRawFont

    return PDFRealizedFont::createRealizedFont(font, fontSize);
    /*
    QRawFont rawFont;

    if (m_fontDescriptor.isEmbedded())
    {
        // Type 1 font
        if (!m_fontDescriptor.fontFile.isEmpty())
        {
            rawFont.loadFromData(m_fontDescriptor.fontFile, fontSize, QFont::PreferNoHinting);
        }
        else if (!m_fontDescriptor.fontFile2.isEmpty())
        {
            rawFont.loadFromData(m_fontDescriptor.fontFile2, fontSize, QFont::PreferNoHinting);
        }

        if (!rawFont.isValid())
        {
            throw PDFParserException(PDFTranslationContext::tr("Can't load embedded font."));
        }
    }
    else
    {
        // TODO: Zkontrolovat, zda se zde opravdu prebiraji spravne fonty
        const int weight = qBound<int>(0, m_fontDescriptor.fontWeight / 10.0, 99);
        const int stretch = qBound<int>(1, m_fontDescriptor.fontStretch, 4000);

        QFont font(m_baseFont);
        font.setHintingPreference(QFont::PreferNoHinting);
        font.setStretch(stretch);
        font.setWeight(weight);
        font.setPixelSize(fontSize);
        rawFont = QRawFont::fromFont(font, QFontDatabase::Any);
    }

    return rawFont;*/
}

QString PDFSimpleFont::getTextUsingEncoding(const QByteArray& byteArray) const
{
    QString string;
    string.resize(byteArray.size(), QChar());

    for (int i = 0, count = byteArray.size(); i < count; ++i)
    {
        string[i] = m_encoding[static_cast<uint8_t>(byteArray[i])];
    }

    return string;
}

PDFType1Font::PDFType1Font(FontDescriptor fontDescriptor,
                           QByteArray name,
                           QByteArray baseFont,
                           PDFInteger firstChar,
                           PDFInteger lastChar,
                           std::vector<PDFInteger> widths,
                           PDFEncoding::Encoding encodingType,
                           encoding::EncodingTable encoding,
                           StandardFontType standardFontType) :
    PDFSimpleFont(qMove(fontDescriptor), qMove(name), qMove(baseFont), firstChar, lastChar, qMove(widths), encodingType, encoding),
    m_standardFontType(standardFontType)
{

}

FontType PDFType1Font::getFontType() const
{
    return FontType::Type1;
}

FontType PDFTrueTypeFont::getFontType() const
{
    return FontType::TrueType;
}

void PDFFontCache::setDocument(const PDFDocument* document)
{
    QMutexLocker lock(&m_mutex);
    if (m_document != document)
    {
        m_document = document;
        m_fontCache.clear();
        m_realizedFontCache.clear();
    }
}

PDFFontPointer PDFFontCache::getFont(const PDFObject& fontObject) const
{
    if (fontObject.isReference())
    {
        // Font is object reference. Look in the cache, if we have it, then return it.

        QMutexLocker lock(&m_mutex);
        PDFObjectReference reference = fontObject.getReference();

        auto it = m_fontCache.find(reference);
        if (it == m_fontCache.cend())
        {
            // We must create the font
            PDFFontPointer font = PDFFont::createFont(fontObject, m_document);

            if (m_fontCache.size() >= m_fontCacheLimit)
            {
                // We have exceeded the cache limit. Clear the cache.
                m_fontCache.clear();
            }

            it = m_fontCache.insert(std::make_pair(reference, qMove(font))).first;
        }
        return it->second;
    }
    else
    {
        // Object is not a reference. Create font directly and return it.
        return PDFFont::createFont(fontObject, m_document);
    }
}

PDFRealizedFontPointer PDFFontCache::getRealizedFont(const PDFFontPointer& font, PDFReal size) const
{
    Q_ASSERT(font);

    QMutexLocker lock(&m_mutex);
    auto it = m_realizedFontCache.find(std::make_pair(font, size));
    if (it == m_realizedFontCache.cend())
    {
        // We must create the realized font
        PDFRealizedFontPointer realizedFont = font->getRealizedFont(font, size);

        if (m_realizedFontCache.size() >= m_realizedFontCacheLimit)
        {
            m_realizedFontCache.clear();
        }

        it = m_realizedFontCache.insert(std::make_pair(std::make_pair(font, size), qMove(realizedFont))).first;
    }

    return it->second;
}

}   // namespace pdf
