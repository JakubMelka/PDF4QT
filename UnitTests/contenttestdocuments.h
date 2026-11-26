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

#ifndef CONTENTTESTDOCUMENTS_H
#define CONTENTTESTDOCUMENTS_H

// The test documents of the content editing (Edit Content, stage E0): the tests of the changes of
// PDFPageContentProcessor and of the packages of the content tree, the operations, the text layout,
// the structure, the commands and the round trip. The documents are built in memory from
// hand-written objects and content streams, by the pattern createDocument(pageContent,
// createResources) of tst_contenteditortest.cpp. Every number, which a test needs, is written
// in the comment of its function, so the expected values of a test are computed by hand from the
// input and from ISO 32000-2, never taken from a run.
//
// Every document has one page with the media box [0 0 200 200] (PAGE_SIZE), no rotation.
// The header needs only Pdf4QtLibCore, Qt6::Core and Qt6::Gui.

#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfobject.h"

#include <QByteArray>
#include <QList>
#include <QRectF>

#include <functional>
#include <utility>
#include <vector>

namespace pdftest
{

/// The test documents of the content editing: static functions only
class ContentTestDocuments
{
public:
    ContentTestDocuments() = delete;

    using DictionaryEntries = std::vector<std::pair<QByteArray, pdf::PDFObject>>;

    /// Creates the resource dictionary of the page; it adds its objects (fonts, forms, ...) to the builder
    using ResourcesFactory = std::function<pdf::PDFDictionaryBuilder(pdf::PDFDocumentBuilder*)>;

    /// Creates the value of /Contents of the page (a reference to a stream, or an array)
    using ContentsFactory = std::function<pdf::PDFObject(pdf::PDFDocumentBuilder*)>;

    /// Changes the document, when the page has its contents and resources (the catalog, the
    /// structure tree, the annotations): it gets the reference of the page
    using PageFinisher = std::function<void(pdf::PDFDocumentBuilder*, pdf::PDFObjectReference)>;

    /// Side of the square page of every document, in points
    static constexpr int PAGE_SIZE = 200;

    // ---------------------------------------------------------------------------------------
    //  Small helpers of the objects
    // ---------------------------------------------------------------------------------------

    /// A dictionary of the entries, in their order
    static inline pdf::PDFObject createDictionaryObject(DictionaryEntries entries)
    {
        pdf::PDFDictionaryBuilder dictionary;
        for (auto& entry : entries)
        {
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString(entry.first), std::move(entry.second));
        }

        return pdf::PDFObject::createDictionary(std::move(dictionary));
    }

    /// An array of the objects
    static inline pdf::PDFObject createArrayObject(std::vector<pdf::PDFObject> items)
    {
        pdf::PDFArrayBuilder array;
        for (pdf::PDFObject& item : items)
        {
            array.appendItem(std::move(item));
        }

        return pdf::PDFObject::createArray(std::move(array));
    }

    /// An array of real numbers
    static inline pdf::PDFObject createNumberArrayObject(std::vector<pdf::PDFReal> numbers)
    {
        pdf::PDFArrayBuilder array;
        for (pdf::PDFReal number : numbers)
        {
            array.appendItem(pdf::PDFObject::createReal(number));
        }

        return pdf::PDFObject::createArray(std::move(array));
    }

    /// An array of integers
    static inline pdf::PDFObject createIntegerArrayObject(std::vector<pdf::PDFInteger> numbers)
    {
        pdf::PDFArrayBuilder array;
        for (pdf::PDFInteger number : numbers)
        {
            array.appendItem(pdf::PDFObject::createInteger(number));
        }

        return pdf::PDFObject::createArray(std::move(array));
    }

    /// Adds an object to the document, returns the reference to it as an object
    static inline pdf::PDFObject addObject(pdf::PDFDocumentBuilder* builder, pdf::PDFObject object)
    {
        return pdf::PDFObject::createReference(builder->addObject(std::move(object)));
    }

    /// Adds a stream with the entries (and /Length) to the document, returns the reference to it
    /// as an object. The data are stored as they are (no filter).
    static inline pdf::PDFObject addStreamObject(pdf::PDFDocumentBuilder* builder, DictionaryEntries entries, QByteArray content)
    {
        pdf::PDFDictionaryBuilder dictionary;
        for (auto& entry : entries)
        {
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString(entry.first), std::move(entry.second));
        }
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(content.size()));

        pdf::PDFObject streamObject = pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(content)));
        return pdf::PDFObject::createReference(builder->addObject(std::move(streamObject)));
    }

    /// Adds one of the 14 standard fonts (Type 1, /WinAnsiEncoding, no /Widths), returns the reference.
    /// Its advances come from the font of the system: a test cannot know them.
    static inline pdf::PDFObject addStandardFontObject(pdf::PDFDocumentBuilder* builder, const char* baseFont)
    {
        pdf::PDFObject fontObject = createDictionaryObject({ { "Type", pdf::PDFObject::createName("Font") },
                                                             { "Subtype", pdf::PDFObject::createName("Type1") },
                                                             { "BaseFont", pdf::PDFObject::createName(baseFont) },
                                                             { "Encoding", pdf::PDFObject::createName("WinAnsiEncoding") } });
        return pdf::PDFObject::createReference(builder->addObject(std::move(fontObject)));
    }

    /// No resources of the page (an empty dictionary)
    static inline pdf::PDFDictionaryBuilder createNoResources(pdf::PDFDocumentBuilder* builder)
    {
        Q_UNUSED(builder);
        return pdf::PDFDictionaryBuilder();
    }

    // ---------------------------------------------------------------------------------------
    //  Generic documents
    // ---------------------------------------------------------------------------------------

    /// The generic document: one page [0 0 200 200], /Contents and /Resources by the factories
    /// \param createContents Value of /Contents
    /// \param createResources Resource dictionary of the page (a direct dictionary of the page)
    /// \param finishPage Optional changes of the document after the page is complete
    static inline pdf::PDFDocument createDocumentFromObjects(const ContentsFactory& createContents,
                                                             const ResourcesFactory& createResources,
                                                             const PageFinisher& finishPage = PageFinisher())
    {
        pdf::PDFDocumentBuilder builder;
        pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, PAGE_SIZE, PAGE_SIZE));

        pdf::PDFObject contentsObject = createContents(&builder);
        pdf::PDFDictionaryBuilder resources = createResources(&builder);

        pdf::PDFDictionaryBuilder pageUpdate;
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(resources)));
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), std::move(contentsObject));
        builder.mergeTo(pageReference, pdf::PDFObject::createDictionary(std::move(pageUpdate)));

        if (finishPage)
        {
            finishPage(&builder, pageReference);
        }

        return builder.build();
    }

    /// A page with one content stream: /Contents is a reference to the stream, whose decoded data
    /// are \p pageContent byte for byte
    static inline pdf::PDFDocument createDocument(QByteArray pageContent, const ResourcesFactory& createResources)
    {
        return createDocumentFromObjects([&pageContent](pdf::PDFDocumentBuilder* builder) { return addStreamObject(builder, DictionaryEntries(), pageContent); },
                                         createResources);
    }

    /// A page with several content streams: /Contents is an array of references, the item i is the
    /// stream of \p contentStreams [i] byte for byte (ISO 32000-2, 7.8.2: the streams are one
    /// content, the division is between two lexical tokens)
    static inline pdf::PDFDocument createDocument(QList<QByteArray> contentStreams, const ResourcesFactory& createResources)
    {
        return createDocumentFromObjects([&contentStreams](pdf::PDFDocumentBuilder* builder)
        {
            std::vector<pdf::PDFObject> items;
            for (const QByteArray& contentStream : contentStreams)
            {
                items.push_back(addStreamObject(builder, DictionaryEntries(), contentStream));
            }
            return createArrayObject(std::move(items));
        }, createResources);
    }

    /// Number of the items of /Contents of createBrokenContentsDocument
    static constexpr int BROKEN_CONTENTS_ITEM_COUNT = 4;

    /// Index of the item of /Contents of createBrokenContentsDocument, which is not a stream
    static constexpr int BROKEN_CONTENTS_NOT_STREAM_INDEX = 1;

    /// Index of the item of /Contents of createBrokenContentsDocument, which cannot be decoded
    static constexpr int BROKEN_CONTENTS_NOT_DECODABLE_INDEX = 2;

    /// The data of the stream of createBrokenContentsDocument, which cannot be decoded: they are
    /// not data of the filter /FlateDecode (ISO 32000-2, 7.4.4: the first byte of a zlib stream has
    /// the compression method 8 in its low four bits, the letter 'n' is 0x6E)
    static inline QByteArray brokenContentsUndecodableData() { return QByteArray("not a flate stream"); }

    /// A page with a broken array /Contents of four items:
    ///   0  a stream with \p firstContent,
    ///   1  a reference to a dictionary << /Type /NotAStream >> (not a stream),
    ///   2  a stream with /Filter /FlateDecode, whose data cannot be decoded,
    ///   3  a stream with \p lastContent.
    /// A reader, which skips the bad items, shows the content of the streams 0 and 3.
    static inline pdf::PDFDocument createBrokenContentsDocument(QByteArray firstContent, QByteArray lastContent, const ResourcesFactory& createResources)
    {
        return createDocumentFromObjects([&firstContent, &lastContent](pdf::PDFDocumentBuilder* builder)
        {
            std::vector<pdf::PDFObject> items;
            items.push_back(addStreamObject(builder, DictionaryEntries(), firstContent));
            items.push_back(addObject(builder, createDictionaryObject({ { "Type", pdf::PDFObject::createName("NotAStream") } })));
            items.push_back(addStreamObject(builder, { { "Filter", pdf::PDFObject::createName("FlateDecode") } }, brokenContentsUndecodableData()));
            items.push_back(addStreamObject(builder, DictionaryEntries(), lastContent));
            return createArrayObject(std::move(items));
        }, createResources);
    }

    // ---------------------------------------------------------------------------------------
    //  Simple font with hand-written widths
    // ---------------------------------------------------------------------------------------

    /// First and last character code of /Widths of the simple font
    static constexpr int SIMPLE_FONT_FIRST_CHAR = 32;
    static constexpr int SIMPLE_FONT_LAST_CHAR = 68;

    /// /MissingWidth of the font descriptor of the simple font: the width of the codes outside of
    /// /FirstChar ... /LastChar (ISO 32000-2, 9.6.2, Table 109 and 9.8.1, Table 120)
    static constexpr int SIMPLE_FONT_MISSING_WIDTH = 400;

    /// The width of a code of the simple font in /Widths, in thousandths of the text space unit
    /// (the glyph space of a Type 1 font): 32 -> 300, 65 (A) -> 700, 66 (B) -> 650, 67 (C) -> 600,
    /// 68 (D) -> 550, the codes 33 ... 64 -> 500. A code outside of 32 ... 68 has
    /// SIMPLE_FONT_MISSING_WIDTH. The widths are not those of Helvetica (space 278, A 667,
    /// B 667, C 722, D 722), so a test sees, where an advance came from.
    static constexpr int simpleFontWidth(int code)
    {
        switch (code)
        {
            case 32:
                return 300;
            case 65:
                return 700;
            case 66:
                return 650;
            case 67:
                return 600;
            case 68:
                return 550;
            default:
                return (code >= SIMPLE_FONT_FIRST_CHAR && code <= SIMPLE_FONT_LAST_CHAR) ? 500 : SIMPLE_FONT_MISSING_WIDTH;
        }
    }

    /// Adds a simple font with hand-written widths and returns the reference to it: Type 1,
    /// /BaseFont /Helvetica, not embedded (the outlines and the advances used by the renderer of
    /// the project come from a font of the system), /FirstChar 32, /LastChar 68, /Widths by
    /// simpleFontWidth, /Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences
    /// \p differences >>, and an indirect /FontDescriptor (/FontName /Helvetica, /Flags 32,
    /// /FontBBox [-166 -225 1000 931], /ItalicAngle 0, /Ascent 718, /Descent -207, /CapHeight 718,
    /// /StemV 88, /MissingWidth 400).
    /// \param differences The array /Differences, see addSimpleFontObject and addSwappedSpaceFontObject
    /// \param firstChar /FirstChar; a code below 32 gets the width SIMPLE_FONT_CONTROL_WIDTH (addControlCodeFontObject)
    static inline pdf::PDFObject addSimpleFontObjectWithDifferences(pdf::PDFDocumentBuilder* builder, pdf::PDFObject differences, int firstChar = SIMPLE_FONT_FIRST_CHAR)
    {
        std::vector<pdf::PDFInteger> widths;
        for (int code = firstChar; code <= SIMPLE_FONT_LAST_CHAR; ++code)
        {
            widths.push_back(code < SIMPLE_FONT_FIRST_CHAR ? SIMPLE_FONT_CONTROL_WIDTH : simpleFontWidth(code));
        }

        pdf::PDFObject fontDescriptor = addObject(builder, createDictionaryObject({ { "Type", pdf::PDFObject::createName("FontDescriptor") },
                                                                                    { "FontName", pdf::PDFObject::createName("Helvetica") },
                                                                                    { "Flags", pdf::PDFObject::createInteger(32) },
                                                                                    { "FontBBox", createIntegerArrayObject({ -166, -225, 1000, 931 }) },
                                                                                    { "ItalicAngle", pdf::PDFObject::createInteger(0) },
                                                                                    { "Ascent", pdf::PDFObject::createInteger(718) },
                                                                                    { "Descent", pdf::PDFObject::createInteger(-207) },
                                                                                    { "CapHeight", pdf::PDFObject::createInteger(718) },
                                                                                    { "StemV", pdf::PDFObject::createInteger(88) },
                                                                                    { "MissingWidth", pdf::PDFObject::createInteger(SIMPLE_FONT_MISSING_WIDTH) } }));

        pdf::PDFObject encoding = createDictionaryObject({ { "Type", pdf::PDFObject::createName("Encoding") },
                                                           { "BaseEncoding", pdf::PDFObject::createName("WinAnsiEncoding") },
                                                           { "Differences", std::move(differences) } });

        return addObject(builder, createDictionaryObject({ { "Type", pdf::PDFObject::createName("Font") },
                                                           { "Subtype", pdf::PDFObject::createName("Type1") },
                                                           { "BaseFont", pdf::PDFObject::createName("Helvetica") },
                                                           { "FirstChar", pdf::PDFObject::createInteger(firstChar) },
                                                           { "LastChar", pdf::PDFObject::createInteger(SIMPLE_FONT_LAST_CHAR) },
                                                           { "Widths", createIntegerArrayObject(std::move(widths)) },
                                                           { "Encoding", std::move(encoding) },
                                                           { "FontDescriptor", std::move(fontDescriptor) } }));
    }

    /// The width of the codes 1 ... 31 of the font of addControlCodeFontObject in /Widths
    static constexpr int SIMPLE_FONT_CONTROL_WIDTH = 450;

    /// The simple font of addSimpleFontObject with /FirstChar 1: the codes 1 ... 31 have the width
    /// 450 in /Widths, but no glyph - WinAnsiEncoding has no character for them (ISO 32000-2,
    /// Annex D.2), so no font program has a glyph to show. Such a code shows nothing and moves
    /// the text by its width as any other code: tx = (0.45 * Tfs + Tc) * Th (9.4.4).
    static inline pdf::PDFObject addControlCodeFontObject(pdf::PDFDocumentBuilder* builder)
    {
        return addSimpleFontObjectWithDifferences(builder, createArrayObject({ pdf::PDFObject::createInteger(67),
                                                                               pdf::PDFObject::createName("Euro"),
                                                                               pdf::PDFObject::createName("space") }), 1);
    }

    /// The simple font with /Differences [67 /Euro 68 /space]: the codes 32 ... 66 are those of
    /// WinAnsiEncoding (32 is the space, 65 is A, 66 is B), the code 67 (the letter C in the string)
    /// shows the glyph Euro (U+20AC) and the code 68 (the letter D) shows the glyph space (U+0020).
    /// The word spacing belongs to the code 32, not to the code 68 (ISO 32000-2, 9.3.3).
    static inline pdf::PDFObject addSimpleFontObject(pdf::PDFDocumentBuilder* builder)
    {
        return addSimpleFontObjectWithDifferences(builder, createArrayObject({ pdf::PDFObject::createInteger(67),
                                                                               pdf::PDFObject::createName("Euro"),
                                                                               pdf::PDFObject::createName("space") }));
    }

    /// The simple font with /Differences [32 /A 65 /space]: the code 32 shows the glyph A
    /// (U+0041) and the code 65 (the letter A in the string) shows the glyph space (U+0020). The
    /// word spacing belongs to the code 32, which is not a space here, and not to the code 65,
    /// which is one (ISO 32000-2, 9.3.3). The widths are those of addSimpleFontObject.
    static inline pdf::PDFObject addSwappedSpaceFontObject(pdf::PDFDocumentBuilder* builder)
    {
        return addSimpleFontObjectWithDifferences(builder, createArrayObject({ pdf::PDFObject::createInteger(32),
                                                                               pdf::PDFObject::createName("A"),
                                                                               pdf::PDFObject::createInteger(65),
                                                                               pdf::PDFObject::createName("space") }));
    }

    // ---------------------------------------------------------------------------------------
    //  Type 0 font
    // ---------------------------------------------------------------------------------------

    /// /DW of the descendant font of the Type 0 font: the width of a CID without an entry of /W
    static constexpr int TYPE0_FONT_DEFAULT_WIDTH = 900;

    /// The width of a CID of the Type 0 font, in thousandths of the text space unit: /W is
    /// [ 32 [250] 65 [700 650] 100 102 480 ], so the CID 32 -> 250, 65 -> 700, 66 -> 650 (the form
    /// "c [w1 w2 ...]"), 100, 101 and 102 -> 480 (the form "c_first c_last w"), every other CID has
    /// /DW 900 (ISO 32000-2, 9.7.4.3). The encoding is /Identity-H: the code of two bytes is the CID.
    static constexpr int type0FontWidth(int cid)
    {
        switch (cid)
        {
            case 32:
                return 250;
            case 65:
                return 700;
            case 66:
                return 650;
            case 100:
            case 101:
            case 102:
                return 480;
            default:
                return TYPE0_FONT_DEFAULT_WIDTH;
        }
    }

    /// The data of the stream /ToUnicode of the Type 0 font (ISO 32000-2, 9.10.3): the codes have
    /// two bytes; <0020> -> U+0020, <0041> -> U+0041 (A), <0042> -> U+0042 (B) and <0064> -> the two
    /// code units U+0066 U+0066 ("ff", one code mapped to more code units). The other codes
    /// (for example <0065>, <0066>) are not mapped.
    static inline QByteArray type0FontToUnicodeData()
    {
        return QByteArray("/CIDInit /ProcSet findresource begin\n"
                          "12 dict begin\n"
                          "begincmap\n"
                          "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                          "/CMapName /Adobe-Identity-UCS def\n"
                          "/CMapType 2 def\n"
                          "1 begincodespacerange\n"
                          "<0000> <FFFF>\n"
                          "endcodespacerange\n"
                          "4 beginbfchar\n"
                          "<0020> <0020>\n"
                          "<0041> <0041>\n"
                          "<0042> <0042>\n"
                          "<0064> <00660066>\n"
                          "endbfchar\n"
                          "endcmap\n"
                          "CMapName currentdict /CMap defineresource pop\n"
                          "end\n"
                          "end\n");
    }

    /// Adds a Type 0 font and returns the reference to it: /BaseFont /Arial, /Encoding /Identity-H
    /// (horizontal, codes of two bytes, code = CID), one descendant font (an indirect object):
    /// /CIDFontType2, /BaseFont /Arial, not embedded, /CIDSystemInfo (Adobe) (Identity) 0, /DW 900,
    /// /W [ 32 [250] 65 [700 650] 100 102 480 ] (type0FontWidth), /CIDToGIDMap /Identity and an
    /// indirect /FontDescriptor (/FontName /Arial, /Flags 32, /FontBBox [-665 -325 2000 1006],
    /// /ItalicAngle 0, /Ascent 905, /Descent -212, /CapHeight 716, /StemV 80); /ToUnicode is the
    /// stream of type0FontToUnicodeData. The string <00410020> is the codes 0x0041 and 0x0020:
    /// the second one is the character U+0020, but not the single-byte code 32, so the word spacing
    /// does not apply to it (ISO 32000-2, 9.3.3).
    static inline pdf::PDFObject addType0FontObject(pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject toUnicode = addStreamObject(builder, DictionaryEntries(), type0FontToUnicodeData());

        return addObject(builder, createDictionaryObject({ { "Type", pdf::PDFObject::createName("Font") },
                                                           { "Subtype", pdf::PDFObject::createName("Type0") },
                                                           { "BaseFont", pdf::PDFObject::createName("Arial") },
                                                           { "Encoding", pdf::PDFObject::createName("Identity-H") },
                                                           { "DescendantFonts", createArrayObject({ addDescendantFontObject(builder) }) },
                                                           { "ToUnicode", std::move(toUnicode) } }));
    }

    /// The data of the CMap stream of the Type 0 font with codes of one and of two bytes (ISO
    /// 32000-2, 9.7.5.4, 9.7.6.2): the codes <00> ... <7F> have one byte, the codes <8000> ... <FFFF>
    /// have two bytes; the CID of every code is its value (32 for <20>, 32800 for <8020>).
    static inline QByteArray type0MixedFontCMapData()
    {
        return QByteArray("/CIDInit /ProcSet findresource begin\n"
                          "12 dict begin\n"
                          "begincmap\n"
                          "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> def\n"
                          "/CMapName /ContentTest-H def\n"
                          "/CMapType 1 def\n"
                          "2 begincodespacerange\n"
                          "<00> <7F>\n"
                          "<8000> <FFFF>\n"
                          "endcodespacerange\n"
                          "2 begincidrange\n"
                          "<00> <7F> 0\n"
                          "<8000> <FFFF> 32768\n"
                          "endcidrange\n"
                          "endcmap\n"
                          "CMapName currentdict /CMap defineresource pop\n"
                          "end\n"
                          "end\n");
    }

    /// The data of the stream /ToUnicode of the Type 0 font with codes of one and of two bytes:
    /// <20> -> U+0020, <41> -> U+0041 (A) and the code of two bytes <8020> -> U+0020 (a space,
    /// whose code is not the single-byte code 32, though its second byte is 32)
    static inline QByteArray type0MixedFontToUnicodeData()
    {
        return QByteArray("/CIDInit /ProcSet findresource begin\n"
                          "12 dict begin\n"
                          "begincmap\n"
                          "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                          "/CMapName /Adobe-Identity-UCS def\n"
                          "/CMapType 2 def\n"
                          "2 begincodespacerange\n"
                          "<00> <7F>\n"
                          "<8000> <FFFF>\n"
                          "endcodespacerange\n"
                          "3 beginbfchar\n"
                          "<20> <0020>\n"
                          "<41> <0041>\n"
                          "<8020> <0020>\n"
                          "endbfchar\n"
                          "endcmap\n"
                          "CMapName currentdict /CMap defineresource pop\n"
                          "end\n"
                          "end\n");
    }

    /// Adds a Type 0 font, which defines the code 32 as a single-byte code, and returns the
    /// reference to it: as addType0FontObject (the same descendant font: /DW 900, the CID 32 ->
    /// 250, 65 -> 700), but /Encoding is the CMap stream of type0MixedFontCMapData (<< /Type /CMap
    /// /CMapName /ContentTest-H /CIDSystemInfo (Adobe) (Identity) 0 /WMode 0 >>) and /ToUnicode is
    /// type0MixedFontToUnicodeData. The string <20418020> is the codes 0x20 (one byte), 0x41 (one
    /// byte) and 0x8020 (two bytes): the word spacing applies to the first one only (ISO 32000-2,
    /// 9.3.3: "a composite font that defines code 32 as a single-byte code"; not to the byte 32 of
    /// a code of more bytes). The widths: 0x20 -> 250, 0x41 -> 700, 0x8020 (the CID 32800) -> 900.
    static inline pdf::PDFObject addType0MixedFontObject(pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject cidSystemInfo = createDictionaryObject({ { "Registry", pdf::PDFObject::createString("Adobe") },
                                                                { "Ordering", pdf::PDFObject::createString("Identity") },
                                                                { "Supplement", pdf::PDFObject::createInteger(0) } });
        pdf::PDFObject encoding = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("CMap") },
                                                             { "CMapName", pdf::PDFObject::createName("ContentTest-H") },
                                                             { "CIDSystemInfo", std::move(cidSystemInfo) },
                                                             { "WMode", pdf::PDFObject::createInteger(0) } }, type0MixedFontCMapData());
        pdf::PDFObject toUnicode = addStreamObject(builder, DictionaryEntries(), type0MixedFontToUnicodeData());

        return addObject(builder, createDictionaryObject({ { "Type", pdf::PDFObject::createName("Font") },
                                                           { "Subtype", pdf::PDFObject::createName("Type0") },
                                                           { "BaseFont", pdf::PDFObject::createName("Arial") },
                                                           { "Encoding", std::move(encoding) },
                                                           { "DescendantFonts", createArrayObject({ addDescendantFontObject(builder) }) },
                                                           { "ToUnicode", std::move(toUnicode) } }));
    }

    /// The data of the CMap stream of the Type 0 font of addType0NoGlyphFontObject: the single-byte
    /// code <20> is the CID 0, the single-byte codes <41> and <42> are the CIDs 65 and 66
    static inline QByteArray type0NoGlyphFontCMapData()
    {
        return QByteArray("/CIDInit /ProcSet findresource begin\n"
                          "12 dict begin\n"
                          "begincmap\n"
                          "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> def\n"
                          "/CMapName /ContentTestNoGlyph-H def\n"
                          "/CMapType 1 def\n"
                          "1 begincodespacerange\n"
                          "<00> <7F>\n"
                          "endcodespacerange\n"
                          "1 begincidchar\n"
                          "<20> 0\n"
                          "endcidchar\n"
                          "1 begincidrange\n"
                          "<41> <42> 65\n"
                          "endcidrange\n"
                          "endcmap\n"
                          "CMapName currentdict /CMap defineresource pop\n"
                          "end\n"
                          "end\n");
    }

    /// The data of the stream /ToUnicode of the Type 0 font of addType0NoGlyphFontObject:
    /// <41> -> U+0041 (A), <42> -> U+0042 (B); the code <20> is not mapped
    static inline QByteArray type0NoGlyphFontToUnicodeData()
    {
        return QByteArray("/CIDInit /ProcSet findresource begin\n"
                          "12 dict begin\n"
                          "begincmap\n"
                          "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                          "/CMapName /Adobe-Identity-UCS def\n"
                          "/CMapType 2 def\n"
                          "1 begincodespacerange\n"
                          "<00> <7F>\n"
                          "endcodespacerange\n"
                          "2 beginbfchar\n"
                          "<41> <0041>\n"
                          "<42> <0042>\n"
                          "endbfchar\n"
                          "endcmap\n"
                          "CMapName currentdict /CMap defineresource pop\n"
                          "end\n"
                          "end\n");
    }

    /// Adds a Type 0 font, whose single-byte code 32 has a width and no glyph, and returns the
    /// reference to it: the descendant font of addType0FontObject (/DW 900, the CID 65 -> 700,
    /// 66 -> 650) under the CMap stream of type0NoGlyphFontCMapData with /ToUnicode of
    /// type0NoGlyphFontToUnicodeData. The code <20> is the CID 0 (the glyph .notdef: a font, which
    /// is not embedded, shows nothing for it) with the default width 900; it is the single-byte
    /// code 32, so it gets the word spacing (ISO 32000-2, 9.3.3): tx = (0.9 * Tfs + Tc + Tw) * Th.
    static inline pdf::PDFObject addType0NoGlyphFontObject(pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject cidSystemInfo = createDictionaryObject({ { "Registry", pdf::PDFObject::createString("Adobe") },
                                                                { "Ordering", pdf::PDFObject::createString("Identity") },
                                                                { "Supplement", pdf::PDFObject::createInteger(0) } });
        pdf::PDFObject encoding = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("CMap") },
                                                             { "CMapName", pdf::PDFObject::createName("ContentTestNoGlyph-H") },
                                                             { "CIDSystemInfo", std::move(cidSystemInfo) },
                                                             { "WMode", pdf::PDFObject::createInteger(0) } }, type0NoGlyphFontCMapData());
        pdf::PDFObject toUnicode = addStreamObject(builder, DictionaryEntries(), type0NoGlyphFontToUnicodeData());

        return addObject(builder, createDictionaryObject({ { "Type", pdf::PDFObject::createName("Font") },
                                                           { "Subtype", pdf::PDFObject::createName("Type0") },
                                                           { "BaseFont", pdf::PDFObject::createName("Arial") },
                                                           { "Encoding", std::move(encoding) },
                                                           { "DescendantFonts", createArrayObject({ addDescendantFontObject(builder) }) },
                                                           { "ToUnicode", std::move(toUnicode) } }));
    }

    /// Adds the descendant font of the Type 0 fonts (see addType0FontObject), returns the reference to it
    static inline pdf::PDFObject addDescendantFontObject(pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject fontDescriptor = addObject(builder, createDictionaryObject({ { "Type", pdf::PDFObject::createName("FontDescriptor") },
                                                                                    { "FontName", pdf::PDFObject::createName("Arial") },
                                                                                    { "Flags", pdf::PDFObject::createInteger(32) },
                                                                                    { "FontBBox", createIntegerArrayObject({ -665, -325, 2000, 1006 }) },
                                                                                    { "ItalicAngle", pdf::PDFObject::createInteger(0) },
                                                                                    { "Ascent", pdf::PDFObject::createInteger(905) },
                                                                                    { "Descent", pdf::PDFObject::createInteger(-212) },
                                                                                    { "CapHeight", pdf::PDFObject::createInteger(716) },
                                                                                    { "StemV", pdf::PDFObject::createInteger(80) } }));

        pdf::PDFObject cidSystemInfo = createDictionaryObject({ { "Registry", pdf::PDFObject::createString("Adobe") },
                                                                { "Ordering", pdf::PDFObject::createString("Identity") },
                                                                { "Supplement", pdf::PDFObject::createInteger(0) } });

        // /W [ 32 [250] 65 [700 650] 100 102 480 ]
        pdf::PDFObject widths = createArrayObject({ pdf::PDFObject::createInteger(32),
                                                    createIntegerArrayObject({ 250 }),
                                                    pdf::PDFObject::createInteger(65),
                                                    createIntegerArrayObject({ 700, 650 }),
                                                    pdf::PDFObject::createInteger(100),
                                                    pdf::PDFObject::createInteger(102),
                                                    pdf::PDFObject::createInteger(480) });

        // /W2 [ 65 [-700 500 880] ]: w1y, v1x, v1y of the CID 65
        pdf::PDFObject verticalWidths = createArrayObject({ pdf::PDFObject::createInteger(65),
                                                            createIntegerArrayObject({ -700, 500, 880 }) });

        return addObject(builder, createDictionaryObject({ { "Type", pdf::PDFObject::createName("Font") },
                                                           { "Subtype", pdf::PDFObject::createName("CIDFontType2") },
                                                           { "BaseFont", pdf::PDFObject::createName("Arial") },
                                                           { "CIDSystemInfo", std::move(cidSystemInfo) },
                                                           { "FontDescriptor", std::move(fontDescriptor) },
                                                           { "DW", pdf::PDFObject::createInteger(TYPE0_FONT_DEFAULT_WIDTH) },
                                                           { "W", std::move(widths) },
                                                           { "DW2", createIntegerArrayObject({ 880, -900 }) },
                                                           { "W2", std::move(verticalWidths) },
                                                           { "CIDToGIDMap", pdf::PDFObject::createName("Identity") } }));
    }

    /// Adds a Type 0 font of the vertical writing mode and returns the reference to it: as
    /// addType0FontObject, but /Encoding /Identity-V. The descendant font of the Type 0 fonts has
    /// the vertical metrics /DW2 [880 -900] and /W2 [65 [-700 500 880]] (ISO 32000-2, 9.7.4.3; a
    /// font of the horizontal writing mode does not use them): the vertical displacement w1 of
    /// the CID 65 (A) is -0.7 and of every other CID -0.9 of the text space unit, so a glyph
    /// moves the text by ty = w1 * Tfs + Tc + Tw (9.4.4), down for a positive font size.
    static inline pdf::PDFObject addType0VerticalFontObject(pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject toUnicode = addStreamObject(builder, DictionaryEntries(), type0FontToUnicodeData());

        return addObject(builder, createDictionaryObject({ { "Type", pdf::PDFObject::createName("Font") },
                                                           { "Subtype", pdf::PDFObject::createName("Type0") },
                                                           { "BaseFont", pdf::PDFObject::createName("Arial") },
                                                           { "Encoding", pdf::PDFObject::createName("Identity-V") },
                                                           { "DescendantFonts", createArrayObject({ addDescendantFontObject(builder) }) },
                                                           { "ToUnicode", std::move(toUnicode) } }));
    }

    // ---------------------------------------------------------------------------------------
    //  Type 3 font
    // ---------------------------------------------------------------------------------------

    enum class Type3Variant
    {
        Plain,              ///< The glyphs paint rectangles only
        SelfReferencing,    ///< The glyph of the code 65 shows the glyph of the code 65 of the same font
        Chain,              ///< Ten glyphs, each one shows the next one
        InnerText,          ///< The glyph of the code 65 shows the glyph of the code 66 in a text object of its own
        UnclosedText,       ///< As InnerText, but the text object of the glyph procedure is not ended
        EndTextOnly,        ///< The glyph procedure of the code 65 has an ET without a BT
        NoSpaceProcedure,   ///< As Plain, but the code 32 has no glyph procedure (only its width)
        StrayOperands       ///< The glyph procedure of the code 65 has an operator with too few operands and leaves operands behind
    };

    /// The first number of /FontMatrix [0.01 0 0 0.01 0 0] of the Type 3 font: 100 units of the
    /// glyph space are one unit of the text space (not the usual 0.001, so a test sees an
    /// assumed 0.001). The width w0 of a glyph in the text space is its width of /Widths * 0.01.
    static constexpr pdf::PDFReal TYPE3_FONT_MATRIX_SCALE = 0.01;

    /// The width of a code of the plain and of the self-referencing Type 3 font in /Widths, in
    /// the units of the glyph space: 32 -> 50, 65 (A) -> 60, 66 (B) -> 80, 67 (C) -> 70, the other
    /// codes of /FirstChar 32 ... /LastChar 67 -> 0. In the text space (* 0.01): 0.5, 0.6, 0.8, 0.7.
    static constexpr int type3FontWidth(int code)
    {
        switch (code)
        {
            case 32:
                return 50;
            case 65:
                return 60;
            case 66:
                return 80;
            case 67:
                return 70;
            default:
                return 0;
        }
    }

    /// The glyph procedure of the code 32 (/space) of the plain Type 3 font: d0, no marks
    static inline QByteArray type3SpaceProcedure() { return QByteArray("50 0 d0"); }

    /// The glyph procedure of the code 65 (/square) of the plain Type 3 font: d0 (a glyph, which
    /// can set its color), a filled square [0 0 50 50] of the glyph space
    static inline QByteArray type3SquareProcedure() { return QByteArray("60 0 d0 0 0 50 50 re f"); }

    /// The glyph procedure of the code 66 (/bar) of the plain and of the self-referencing Type 3
    /// font: d1 with the box [0 0 20 100], a filled rectangle [0 0 20 100] of the glyph space
    static inline QByteArray type3BarProcedure() { return QByteArray("80 0 0 0 20 100 d1 0 0 20 100 re f"); }

    /// The glyph procedure of the code 65 (/square) of the self-referencing Type 3 font: the
    /// square of type3SquareProcedure, then the glyph shows the code 65 of its own font (the
    /// resource /T3 of the font is the font itself) - a reader without a guard never ends
    static inline QByteArray type3SelfReferencingProcedure() { return QByteArray("60 0 d0 0 0 50 50 re f BT /T3 1 Tf (A) Tj ET"); }

    /// The glyph procedure of the code 65 (/square) of the Type 3 font of the variant InnerText:
    /// the square of type3SquareProcedure, then a text object of the procedure (ISO 32000-2, 9.6.4:
    /// a glyph procedure is a content stream of its own), which shows the code 66 (/bar) of the
    /// same font at the size 1 at the point (30, 40) of the glyph space. The glyph space of the
    /// inner glyph is 0.01 * 1 of the glyph space of the outer one, so the inner bar [0 0 20 100]
    /// covers [30 40 30.2 41] of the glyph space of the outer glyph.
    static inline QByteArray type3InnerTextProcedure() { return QByteArray("60 0 d0 0 0 50 50 re f BT /T3 1 Tf 30 40 Td (B) Tj ET"); }

    /// The glyph procedure of the code 65 (/square) of the Type 3 font of the variant
    /// UnclosedText: type3InnerTextProcedure without its ET - a wrong procedure, whose text object
    /// ends with the procedure and never reaches the content, which shows the glyph
    static inline QByteArray type3UnclosedTextProcedure() { return QByteArray("60 0 d0 0 0 50 50 re f BT /T3 1 Tf 30 40 Td (B) Tj"); }

    /// The glyph procedure of the code 65 (/square) of the Type 3 font of the variant EndTextOnly:
    /// the square and an ET without a BT - a wrong procedure, whose ET must not end the text
    /// object of the content, which shows the glyph
    static inline QByteArray type3EndTextOnlyProcedure() { return QByteArray("60 0 d0 0 0 50 50 re f ET"); }

    /// The glyph procedure of the code 65 (/square) of the Type 3 font of the variant
    /// StrayOperands, a wrong procedure: after d0 its operator rg has two operands instead of
    /// three (it fails, the fill color stays the one of the text), then the square is filled,
    /// and the three operands 7 8 9 are left without an operator at its end
    static inline QByteArray type3StrayOperandsProcedure() { return QByteArray("60 0 d0 0 0 rg 0 0 50 50 re f 7 8 9"); }

    /// The glyph procedure of the code 65 (/square) of the Type 3 font of the variant
    static inline QByteArray type3SquareProcedure(Type3Variant variant)
    {
        switch (variant)
        {
            case Type3Variant::StrayOperands:
                return type3StrayOperandsProcedure();
            case Type3Variant::SelfReferencing:
                return type3SelfReferencingProcedure();
            case Type3Variant::InnerText:
                return type3InnerTextProcedure();
            case Type3Variant::UnclosedText:
                return type3UnclosedTextProcedure();
            case Type3Variant::EndTextOnly:
                return type3EndTextOnlyProcedure();
            default:
                return type3SquareProcedure();
        }
    }

    /// Number of the glyphs of the chain Type 3 font: the codes 97 (a) ... 106 (j), each one with
    /// the width 10 of the glyph space. It is more than the limit of the nesting of the glyph
    /// procedures of the processor (PDFPageContentProcessor::MAXIMUM_TYPE3_GLYPH_NESTING, 8).
    static constexpr int TYPE3_CHAIN_LENGTH = 10;

    /// First character code of the chain Type 3 font
    static constexpr int TYPE3_CHAIN_FIRST_CHAR = 97;

    /// The glyph procedure of the glyph \p index (0 ... 9, the code 97 + index) of the chain
    /// Type 3 font: "10 0 d0", a filled square [index 0 index+1 1] of the glyph space, and - except
    /// the last glyph - the text of the next code in the same font at the size 1:
    /// "10 0 d0 3 0 1 1 re f BT /T3 1 Tf (e) Tj ET" for the index 3
    static inline QByteArray type3ChainProcedure(int index)
    {
        QByteArray procedure = "10 0 d0 " + QByteArray::number(index) + " 0 1 1 re f";
        if (index + 1 < TYPE3_CHAIN_LENGTH)
        {
            procedure += " BT /T3 1 Tf (" + QByteArray(1, char(TYPE3_CHAIN_FIRST_CHAR + index + 1)) + ") Tj ET";
        }
        return procedure;
    }

    /// The data of the stream /ToUnicode of the plain and of the self-referencing Type 3 font:
    /// the codes have one byte; <20> -> U+0020, <41> -> U+0041 (A), <42> -> U+0042 (B). The code 67
    /// is not mapped.
    static inline QByteArray type3FontToUnicodeData()
    {
        return QByteArray("/CIDInit /ProcSet findresource begin\n"
                          "12 dict begin\n"
                          "begincmap\n"
                          "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                          "/CMapName /Adobe-Identity-UCS def\n"
                          "/CMapType 2 def\n"
                          "1 begincodespacerange\n"
                          "<00> <FF>\n"
                          "endcodespacerange\n"
                          "3 beginbfchar\n"
                          "<20> <0020>\n"
                          "<41> <0041>\n"
                          "<42> <0042>\n"
                          "endbfchar\n"
                          "endcmap\n"
                          "CMapName currentdict /CMap defineresource pop\n"
                          "end\n"
                          "end\n");
    }

    /// Adds a Type 3 font and returns the reference to it (the font is an indirect object).
    ///
    /// Plain and SelfReferencing: /FontBBox [0 0 100 100], /FontMatrix [0.01 0 0 0.01 0 0],
    /// /FirstChar 32, /LastChar 67, /Widths by type3FontWidth (36 numbers), /Encoding << /Type
    /// /Encoding /Differences [32 /space 65 /square /bar /nothing] >> (the code 65 is /square, 66
    /// is /bar, 67 is /nothing), /CharProcs with the indirect streams /space (type3SpaceProcedure),
    /// /square (type3SquareProcedure, or type3SelfReferencingProcedure) and /bar
    /// (type3BarProcedure) - the code 67 (/nothing) has a width (70), but no procedure -,
    /// /ToUnicode (type3FontToUnicodeData), /Resources << /ProcSet [/PDF] >> for Plain and
    /// << /ProcSet [/PDF] /Font << /T3 (the font itself) >> >> for SelfReferencing.
    ///
    /// InnerText, UnclosedText, EndTextOnly and StrayOperands are SelfReferencing with another
    /// procedure of /square (type3SquareProcedure of the variant). NoSpaceProcedure is Plain without the
    /// procedure /space: /Differences [32 /nospace 65 /square /bar /nothing], so the code 32 has
    /// its width 50 and no glyph procedure.
    ///
    /// Chain: /FontBBox [0 0 10 1], the same /FontMatrix, /FirstChar 97, /LastChar 106, /Widths ten
    /// times 10, /Differences [97 /g0 /g1 ... /g9], /CharProcs /g0 ... /g9 (type3ChainProcedure),
    /// /Resources << /ProcSet [/PDF] /Font << /T3 (the font itself) >> >>, no /ToUnicode.
    static inline pdf::PDFObject addType3FontObject(pdf::PDFDocumentBuilder* builder, Type3Variant variant = Type3Variant::Plain)
    {
        // The font refers to itself: its reference is known, before its dictionary is written
        const pdf::PDFObjectReference fontReference = builder->addObject(pdf::PDFObject());
        pdf::PDFObject fontReferenceObject = pdf::PDFObject::createReference(fontReference);

        DictionaryEntries resources = { { "ProcSet", createArrayObject({ pdf::PDFObject::createName("PDF") }) } };
        if (variant != Type3Variant::Plain && variant != Type3Variant::NoSpaceProcedure)
        {
            resources.emplace_back("Font", createDictionaryObject({ { "T3", fontReferenceObject } }));
        }

        DictionaryEntries font = { { "Type", pdf::PDFObject::createName("Font") },
                                   { "Subtype", pdf::PDFObject::createName("Type3") },
                                   { "FontMatrix", createNumberArrayObject({ TYPE3_FONT_MATRIX_SCALE, 0, 0, TYPE3_FONT_MATRIX_SCALE, 0, 0 }) },
                                   { "Resources", createDictionaryObject(std::move(resources)) } };

        if (variant == Type3Variant::Chain)
        {
            std::vector<pdf::PDFObject> differences = { pdf::PDFObject::createInteger(TYPE3_CHAIN_FIRST_CHAR) };
            DictionaryEntries procedures;
            std::vector<pdf::PDFInteger> widths;
            for (int i = 0; i < TYPE3_CHAIN_LENGTH; ++i)
            {
                const QByteArray name = "g" + QByteArray::number(i);
                differences.push_back(pdf::PDFObject::createName(name));
                procedures.emplace_back(name, addStreamObject(builder, DictionaryEntries(), type3ChainProcedure(i)));
                widths.push_back(10);
            }

            font.emplace_back("FontBBox", createIntegerArrayObject({ 0, 0, 10, 1 }));
            font.emplace_back("FirstChar", pdf::PDFObject::createInteger(TYPE3_CHAIN_FIRST_CHAR));
            font.emplace_back("LastChar", pdf::PDFObject::createInteger(TYPE3_CHAIN_FIRST_CHAR + TYPE3_CHAIN_LENGTH - 1));
            font.emplace_back("Widths", createIntegerArrayObject(std::move(widths)));
            font.emplace_back("Encoding", createDictionaryObject({ { "Type", pdf::PDFObject::createName("Encoding") },
                                                                   { "Differences", createArrayObject(std::move(differences)) } }));
            font.emplace_back("CharProcs", createDictionaryObject(std::move(procedures)));
        }
        else
        {
            std::vector<pdf::PDFInteger> widths;
            for (int code = 32; code <= 67; ++code)
            {
                widths.push_back(type3FontWidth(code));
            }

            const bool hasSpaceProcedure = variant != Type3Variant::NoSpaceProcedure;
            DictionaryEntries procedures = { { "square", addStreamObject(builder, DictionaryEntries(), type3SquareProcedure(variant)) },
                                             { "bar", addStreamObject(builder, DictionaryEntries(), type3BarProcedure()) } };
            if (hasSpaceProcedure)
            {
                procedures.emplace_back("space", addStreamObject(builder, DictionaryEntries(), type3SpaceProcedure()));
            }

            font.emplace_back("FontBBox", createIntegerArrayObject({ 0, 0, 100, 100 }));
            font.emplace_back("FirstChar", pdf::PDFObject::createInteger(32));
            font.emplace_back("LastChar", pdf::PDFObject::createInteger(67));
            font.emplace_back("Widths", createIntegerArrayObject(std::move(widths)));
            font.emplace_back("Encoding", createDictionaryObject({ { "Type", pdf::PDFObject::createName("Encoding") },
                                                                   { "Differences", createArrayObject({ pdf::PDFObject::createInteger(32),
                                                                                                        pdf::PDFObject::createName(hasSpaceProcedure ? "space" : "nospace"),
                                                                                                        pdf::PDFObject::createInteger(65),
                                                                                                        pdf::PDFObject::createName("square"),
                                                                                                        pdf::PDFObject::createName("bar"),
                                                                                                        pdf::PDFObject::createName("nothing") }) } }));
            font.emplace_back("CharProcs", createDictionaryObject(std::move(procedures)));
            font.emplace_back("ToUnicode", addStreamObject(builder, DictionaryEntries(), type3FontToUnicodeData()));
        }

        builder->setObject(fontReference, createDictionaryObject(std::move(font)));
        return fontReferenceObject;
    }

    // ---------------------------------------------------------------------------------------
    //  Fonts together
    // ---------------------------------------------------------------------------------------

    /// The resources of createFontsDocument: /Font << /F1 (addSimpleFontObject) /F2
    /// (addSwappedSpaceFontObject) /FC (addControlCodeFontObject) /F0 (addType0FontObject) /FM
    /// (addType0MixedFontObject) /FN (addType0NoGlyphFontObject) /FV (addType0VerticalFontObject)
    /// /T3 (addType3FontObject, Plain) /TN (addType3FontObject, NoSpaceProcedure) >>
    static inline pdf::PDFDictionaryBuilder createFontResources(pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), createDictionaryObject({ { "F1", addSimpleFontObject(builder) },
                                                                                           { "F2", addSwappedSpaceFontObject(builder) },
                                                                                           { "FC", addControlCodeFontObject(builder) },
                                                                                           { "F0", addType0FontObject(builder) },
                                                                                           { "FM", addType0MixedFontObject(builder) },
                                                                                           { "FN", addType0NoGlyphFontObject(builder) },
                                                                                           { "FV", addType0VerticalFontObject(builder) },
                                                                                           { "T3", addType3FontObject(builder, Type3Variant::Plain) },
                                                                                           { "TN", addType3FontObject(builder, Type3Variant::NoSpaceProcedure) } }));
        return resources;
    }

    /// A page with \p pageContent and the fonts of createFontResources (/F1, /F2, /FC, /F0, /FM,
    /// /FN, /FV, /T3, /TN)
    static inline pdf::PDFDocument createFontsDocument(QByteArray pageContent)
    {
        return createDocument(std::move(pageContent), &ContentTestDocuments::createFontResources);
    }

    /// A page with \p pageContent, the Type 3 font /T3 of the variant and the standard font /F1
    /// (addStandardFontObject, Helvetica)
    static inline pdf::PDFDocument createType3Document(QByteArray pageContent, Type3Variant variant)
    {
        return createDocument(std::move(pageContent), [variant](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), createDictionaryObject({ { "T3", addType3FontObject(builder, variant) },
                                                                                               { "F1", addStandardFontObject(builder, "Helvetica") } }));
            return resources;
        });
    }

    // ---------------------------------------------------------------------------------------
    //  Form XObjects
    // ---------------------------------------------------------------------------------------

    /// The content of the form /Fm1 of createFormDocument: the line width 4 from the resource /GF
    /// of the form, a green rectangle [5 5 25 15] (x 5 ... 25, y 5 ... 15 of the form space), filled
    static inline QByteArray formContent() { return QByteArray("/GF gs 0 1 0 rg 5 5 20 10 re f"); }

    /// The content of the form /Fm2 of createFormDocument (a form without /Resources): the line
    /// width 6 from the resource /GP of the page, a red square [0 0 10 10], filled
    static inline QByteArray formWithoutResourcesContent() { return QByteArray("/GP gs 1 0 0 rg 0 0 10 10 re f"); }

    /// The content of the page of createFormDocument: /Fm1 is drawn twice (the second one moved
    /// by 100 to the right), then /Fm2 once
    static inline QByteArray formPageContent()
    {
        return QByteArray("q /Fm1 Do Q\n"
                          "q 1 0 0 1 100 0 cm /Fm1 Do Q\n"
                          "/Fm2 Do\n");
    }

    /// Adds the form XObject /Fm1 of createFormDocument, returns the reference to it: /BBox
    /// [0 0 40 30], /Matrix [2 0 0 2 10 20], /Resources << /ExtGState << /GF << /Type /ExtGState
    /// /LW 4 >> >> >>, the content formContent. A point (x, y) of the form is the point (2x + 10,
    /// 2y + 20) of the space, where the form is drawn: its box covers x 10 ... 90, y 20 ... 80 and its
    /// rectangle x 20 ... 60, y 30 ... 50.
    static inline pdf::PDFObject addFormObject(pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject graphicState = createDictionaryObject({ { "Type", pdf::PDFObject::createName("ExtGState") },
                                                               { "LW", pdf::PDFObject::createInteger(4) } });
        pdf::PDFObject resources = createDictionaryObject({ { "ExtGState", createDictionaryObject({ { "GF", std::move(graphicState) } }) } });

        return addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                          { "Subtype", pdf::PDFObject::createName("Form") },
                                          { "BBox", createIntegerArrayObject({ 0, 0, 40, 30 }) },
                                          { "Matrix", createIntegerArrayObject({ 2, 0, 0, 2, 10, 20 }) },
                                          { "Resources", std::move(resources) } }, formContent());
    }

    /// Adds the form XObject /Fm2 of createFormDocument, returns the reference to it: /BBox
    /// [0 0 10 10], no /Matrix (the identity) and no /Resources - the names of its content are
    /// those of the page, which draws it (ISO 32000-2, 7.8.3, a form of PDF 1.1); the content
    /// formWithoutResourcesContent
    static inline pdf::PDFObject addFormWithoutResourcesObject(pdf::PDFDocumentBuilder* builder)
    {
        return addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                          { "Subtype", pdf::PDFObject::createName("Form") },
                                          { "BBox", createIntegerArrayObject({ 0, 0, 10, 10 }) } }, formWithoutResourcesContent());
    }

    /// A page drawing a form twice and a form without resources once: the content formPageContent,
    /// the resources /XObject << /Fm1 (addFormObject) /Fm2 (addFormWithoutResourcesObject) >> and
    /// /ExtGState << /GP << /Type /ExtGState /LW 6 >> >>. On the page the green rectangle of /Fm1
    /// is at x 20 ... 60, y 30 ... 50 and at x 120 ... 160, y 30 ... 50; the red square of /Fm2 is at
    /// x 0 ... 10, y 0 ... 10.
    static inline pdf::PDFDocument createFormDocument()
    {
        return createDocument(formPageContent(), [](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFObject graphicState = createDictionaryObject({ { "Type", pdf::PDFObject::createName("ExtGState") },
                                                                   { "LW", pdf::PDFObject::createInteger(6) } });

            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", addFormObject(builder) },
                                                                                              { "Fm2", addFormWithoutResourcesObject(builder) } }));
            resources.addEntry(pdf::PDFInplaceOrMemoryString("ExtGState"), createDictionaryObject({ { "GP", std::move(graphicState) } }));
            return resources;
        });
    }

    // ---------------------------------------------------------------------------------------
    //  Inline images
    // ---------------------------------------------------------------------------------------

    enum class InlineImageVariant
    {
        Plain,              ///< 2 x 2 samples, DeviceGray, the data "abcd"
        EndMarkerInData,    ///< 2 x 2 samples, DeviceGray, the data "aEIb" contain the bytes EI
        NamedColorSpace     ///< 1 x 2 samples, the color space /Cs1 of the resources (three components), the data "abEIcd"
    };

    /// The data of the inline image of the variant: 8 bits per component, no filter. Plain: the
    /// four gray samples 0x61 0x62 0x63 0x64. EndMarkerInData: 0x61 0x45 0x49 0x62 - the bytes
    /// 1 and 2 are "EI". NamedColorSpace: two samples of three components, 0x61 0x62 0x45 and
    /// 0x49 0x63 0x64 - the bytes 2 and 3 are "EI"; a reader, which takes /Cs1 for one component,
    /// reads two bytes and finds that EI right after them.
    static inline QByteArray inlineImageData(InlineImageVariant variant)
    {
        switch (variant)
        {
            case InlineImageVariant::EndMarkerInData:
                return QByteArray("aEIb");
            case InlineImageVariant::NamedColorSpace:
                return QByteArray("abEIcd");
            default:
                return QByteArray("abcd");
        }
    }

    /// The entries of the inline image of the variant, the text between BI and ID:
    /// " /W 2 /H 2 /BPC 8 /CS /G " or " /W 1 /H 2 /BPC 8 /CS /Cs1 "
    static inline QByteArray inlineImageDictionary(InlineImageVariant variant)
    {
        return (variant == InlineImageVariant::NamedColorSpace) ? QByteArray(" /W 1 /H 2 /BPC 8 /CS /Cs1 ") : QByteArray(" /W 2 /H 2 /BPC 8 /CS /G ");
    }

    /// The content of the page of createInlineImageDocument:
    ///   "q 20 0 0 20 10 10 cm\nBI" + inlineImageDictionary + "ID " + inlineImageData + "\nEI\nQ\n0 0 5 5 re f\n"
    /// The image covers x 10 ... 30, y 10 ... 30 of the page. The letters ID and EI occur nowhere else
    /// than as the operators - except the EI in the data of the two variants.
    static inline QByteArray inlineImagePageContent(InlineImageVariant variant)
    {
        return "q 20 0 0 20 10 10 cm\nBI" + inlineImageDictionary(variant) + "ID " + inlineImageData(variant) + "\nEI\nQ\n0 0 5 5 re f\n";
    }

    /// A page with one inline image (inlineImagePageContent). The resources: /ColorSpace << /Cs1
    /// [/CalRGB << /WhitePoint [0.9505 1 1.089] >>] >> (three components; ISO 32000-2, 8.9.7: the
    /// name of a color space of an inline image, which is not a device space, is looked up in the
    /// resources).
    static inline pdf::PDFDocument createInlineImageDocument(InlineImageVariant variant)
    {
        return createDocument(inlineImagePageContent(variant), [](pdf::PDFDocumentBuilder* builder)
        {
            Q_UNUSED(builder);

            pdf::PDFObject calRGB = createArrayObject({ pdf::PDFObject::createName("CalRGB"),
                                                        createDictionaryObject({ { "WhitePoint", createNumberArrayObject({ 0.9505, 1.0, 1.089 }) } }) });

            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), createDictionaryObject({ { "Cs1", std::move(calRGB) } }));
            return resources;
        });
    }

    // ---------------------------------------------------------------------------------------
    //  Tagged page
    // ---------------------------------------------------------------------------------------

    /// The marked content identifiers of the page of createTaggedDocument, in the order of the
    /// content: 0 (/P), 1 (/Span), 3 (/Figure). The identifier 2 is not used: the item 2 of the
    /// array of the page in the parent tree is null.
    static inline std::vector<pdf::PDFInteger> taggedPageMarkedContentIdentifiers() { return { 0, 1, 3 }; }

    /// The content of the page of createTaggedDocument: four marked content sequences
    ///   /P << /MCID 0 >> BDC ... EMC        the text (First) at 20 150, font /F1 12
    ///   /Span << /MCID 1 >> BDC ... EMC     the text (Second) at 20 130, font /F1 12
    ///   /Artifact BMC ... EMC               a filled rectangle [0 0 200 10], no identifier
    ///   /Figure << /MCID 3 >> BDC ... EMC   a filled square [20 20 70 70]
    static inline QByteArray taggedPageContent()
    {
        return QByteArray("/P << /MCID 0 >> BDC\n"
                          "BT /F1 12 Tf 20 150 Td (First) Tj ET\n"
                          "EMC\n"
                          "/Span << /MCID 1 >> BDC\n"
                          "BT /F1 12 Tf 20 130 Td (Second) Tj ET\n"
                          "EMC\n"
                          "/Artifact BMC\n"
                          "0 0 200 10 re f\n"
                          "EMC\n"
                          "/Figure << /MCID 3 >> BDC\n"
                          "20 20 50 50 re f\n"
                          "EMC\n");
    }

    /// A tagged page (ISO 32000-2, 14.7 and 14.8). The content is taggedPageContent, the font /F1
    /// is addStandardFontObject (Helvetica). The page has /StructParents 0, /Tabs /S and one link
    /// annotation (/Rect [20 148 60 162], /Border [0 0 0], /StructParent 1, no action). The catalog
    /// has /MarkInfo << /Marked true >>, /Lang (en-US) and /StructTreeRoot:
    ///
    ///   StructTreeRoot  /K [Document], /ParentTree, /ParentTreeNextKey 2
    ///     Document      /S /Document, /K [P Span Figure Link]
    ///       P           /S /P, /Pg page, /K 0                                 (an integer identifier)
    ///       Span        /S /Span, /Pg page, /K << /Type /MCR /Pg page /MCID 1 >>   (a marked content reference)
    ///       Figure      /S /Figure, /Pg page, /Alt (Square), /K [3]           (an integer in an array)
    ///       Link        /S /Link, /Pg page, /K << /Type /OBJR /Obj annotation >>   (an object reference)
    ///
    /// The parent tree is a number tree with one node: /Nums [ 0 [P Span null Figure] 1 Link ] - the
    /// key 0 (/StructParents of the page) is the array indexed by the identifier, with a null for
    /// the unused identifier 2; the key 1 (/StructParent of the annotation) is the element Link
    /// (14.7.5.4). All structure elements, the root, the parent tree and the annotation are
    /// indirect objects.
    static inline pdf::PDFDocument createTaggedDocument()
    {
        return createDocumentFromObjects([](pdf::PDFDocumentBuilder* builder) { return addStreamObject(builder, DictionaryEntries(), taggedPageContent()); },
                                         [](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), createDictionaryObject({ { "F1", addStandardFontObject(builder, "Helvetica") } }));
            return resources;
        },
                                         [](pdf::PDFDocumentBuilder* builder, pdf::PDFObjectReference pageReference)
        {
            pdf::PDFObject page = pdf::PDFObject::createReference(pageReference);

            // The objects refer to each other: the references first
            pdf::PDFObject root = pdf::PDFObject::createReference(builder->addObject(pdf::PDFObject()));
            pdf::PDFObject documentElement = pdf::PDFObject::createReference(builder->addObject(pdf::PDFObject()));
            pdf::PDFObject paragraphElement = pdf::PDFObject::createReference(builder->addObject(pdf::PDFObject()));
            pdf::PDFObject spanElement = pdf::PDFObject::createReference(builder->addObject(pdf::PDFObject()));
            pdf::PDFObject figureElement = pdf::PDFObject::createReference(builder->addObject(pdf::PDFObject()));
            pdf::PDFObject linkElement = pdf::PDFObject::createReference(builder->addObject(pdf::PDFObject()));

            pdf::PDFObject annotation = addObject(builder, createDictionaryObject({ { "Type", pdf::PDFObject::createName("Annot") },
                                                                                    { "Subtype", pdf::PDFObject::createName("Link") },
                                                                                    { "Rect", createIntegerArrayObject({ 20, 148, 60, 162 }) },
                                                                                    { "Border", createIntegerArrayObject({ 0, 0, 0 }) },
                                                                                    { "P", page },
                                                                                    { "StructParent", pdf::PDFObject::createInteger(1) } }));

            // /Nums [ 0 [P Span null Figure] 1 Link ]
            pdf::PDFObject parentTree = addObject(builder, createDictionaryObject({ { "Nums", createArrayObject({ pdf::PDFObject::createInteger(0),
                                                                                                             createArrayObject({ paragraphElement, spanElement, pdf::PDFObject::createNull(), figureElement }),
                                                                                                             pdf::PDFObject::createInteger(1),
                                                                                                             linkElement }) } }));

            builder->setObject(root.getReference(), createDictionaryObject({ { "Type", pdf::PDFObject::createName("StructTreeRoot") },
                                                                             { "K", createArrayObject({ documentElement }) },
                                                                             { "ParentTree", std::move(parentTree) },
                                                                             { "ParentTreeNextKey", pdf::PDFObject::createInteger(2) } }));

            builder->setObject(documentElement.getReference(), createDictionaryObject({ { "Type", pdf::PDFObject::createName("StructElem") },
                                                                                        { "S", pdf::PDFObject::createName("Document") },
                                                                                        { "P", root },
                                                                                        { "K", createArrayObject({ paragraphElement, spanElement, figureElement, linkElement }) } }));

            builder->setObject(paragraphElement.getReference(), createDictionaryObject({ { "Type", pdf::PDFObject::createName("StructElem") },
                                                                                         { "S", pdf::PDFObject::createName("P") },
                                                                                         { "P", documentElement },
                                                                                         { "Pg", page },
                                                                                         { "K", pdf::PDFObject::createInteger(0) } }));

            builder->setObject(spanElement.getReference(), createDictionaryObject({ { "Type", pdf::PDFObject::createName("StructElem") },
                                                                                    { "S", pdf::PDFObject::createName("Span") },
                                                                                    { "P", documentElement },
                                                                                    { "Pg", page },
                                                                                    { "K", createDictionaryObject({ { "Type", pdf::PDFObject::createName("MCR") },
                                                                                                                    { "Pg", page },
                                                                                                                    { "MCID", pdf::PDFObject::createInteger(1) } }) } }));

            builder->setObject(figureElement.getReference(), createDictionaryObject({ { "Type", pdf::PDFObject::createName("StructElem") },
                                                                                      { "S", pdf::PDFObject::createName("Figure") },
                                                                                      { "P", documentElement },
                                                                                      { "Pg", page },
                                                                                      { "Alt", pdf::PDFObject::createString("Square") },
                                                                                      { "K", createIntegerArrayObject({ 3 }) } }));

            builder->setObject(linkElement.getReference(), createDictionaryObject({ { "Type", pdf::PDFObject::createName("StructElem") },
                                                                                    { "S", pdf::PDFObject::createName("Link") },
                                                                                    { "P", documentElement },
                                                                                    { "Pg", page },
                                                                                    { "K", createDictionaryObject({ { "Type", pdf::PDFObject::createName("OBJR") },
                                                                                                                    { "Obj", annotation } }) } }));

            builder->mergeTo(pageReference, createDictionaryObject({ { "StructParents", pdf::PDFObject::createInteger(0) },
                                                                     { "Tabs", pdf::PDFObject::createName("S") },
                                                                     { "Annots", createArrayObject({ annotation }) } }));

            builder->mergeTo(builder->getCatalogReference(), createDictionaryObject({ { "MarkInfo", createDictionaryObject({ { "Marked", pdf::PDFObject::createBool(true) } }) },
                                                                                      { "Lang", pdf::PDFObject::createString("en-US") },
                                                                                      { "StructTreeRoot", root } }));
        });
    }

    // ---------------------------------------------------------------------------------------
    //  Extended graphic states
    // ---------------------------------------------------------------------------------------

    /// The content of the transparency group of the soft mask of createSoftMaskDocument: the gray
    /// 0.5 fills the left half [0 0 100 200] of the page; the rest keeps the backdrop black
    static inline QByteArray softMaskGroupContent() { return QByteArray("0.5 g 0 0 100 200 re f"); }

    /// The content of the page of createSoftMaskDocument: a red rectangle [0 0 200 100] under the
    /// soft mask of /GS1, then a blue rectangle [0 100 200 200] filled and stroked with the
    /// constant alphas of /GS2
    static inline QByteArray softMaskPageContent()
    {
        return QByteArray("q /GS1 gs 1 0 0 rg 0 0 200 100 re f Q\n"
                          "q /GS2 gs 0 0 1 rg 0 0 1 RG 0 100 200 100 re B Q\n");
    }

    /// A page with two extended graphic states (softMaskPageContent):
    ///   /GS1 << /Type /ExtGState /SMask << /Type /Mask /S /Luminosity /G (form) >> >> - the form is
    ///        an indirect transparency group XObject: /BBox [0 0 200 200], /Group << /S /Transparency
    ///        /CS /DeviceGray >>, the content softMaskGroupContent. The mask is 0.5 for x < 100 and 0
    ///        (the backdrop of the group is black, 11.6.5.2) for x >= 100.
    ///   /GS2 << /Type /ExtGState /ca 0.5 /CA 0.25 >> - only the constant alphas: 0.5 for filling,
    ///        0.25 for stroking.
    /// Both graphic states are direct dictionaries of the resources.
    static inline pdf::PDFDocument createSoftMaskDocument()
    {
        return createDocument(softMaskPageContent(), [](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFObject group = createDictionaryObject({ { "S", pdf::PDFObject::createName("Transparency") },
                                                            { "CS", pdf::PDFObject::createName("DeviceGray") } });
            pdf::PDFObject form = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                             { "Subtype", pdf::PDFObject::createName("Form") },
                                                             { "BBox", createIntegerArrayObject({ 0, 0, 200, 200 }) },
                                                             { "Group", std::move(group) } }, softMaskGroupContent());

            pdf::PDFObject softMask = createDictionaryObject({ { "Type", pdf::PDFObject::createName("Mask") },
                                                               { "S", pdf::PDFObject::createName("Luminosity") },
                                                               { "G", std::move(form) } });
            pdf::PDFObject graphicState1 = createDictionaryObject({ { "Type", pdf::PDFObject::createName("ExtGState") },
                                                                    { "SMask", std::move(softMask) } });
            pdf::PDFObject graphicState2 = createDictionaryObject({ { "Type", pdf::PDFObject::createName("ExtGState") },
                                                                    { "ca", pdf::PDFObject::createReal(0.5) },
                                                                    { "CA", pdf::PDFObject::createReal(0.25) } });

            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("ExtGState"), createDictionaryObject({ { "GS1", std::move(graphicState1) },
                                                                                                { "GS2", std::move(graphicState2) } }));
            return resources;
        });
    }

    // ---------------------------------------------------------------------------------------
    //  Tiling pattern
    // ---------------------------------------------------------------------------------------

    /// The content of the cell of the tiling pattern of createTilingPatternDocument: the line width
    /// 2 from the resource /GT of the pattern, a red square [0 0 10 10] (the whole box), filled
    static inline QByteArray tilingPatternContent() { return QByteArray("/GT gs 1 0 0 rg 0 0 10 10 re f"); }

    /// The content of the page of createTilingPatternDocument: the square [25 25 65 65] filled with
    /// the pattern /P1
    static inline QByteArray tilingPatternPageContent() { return QByteArray("/Pattern cs /P1 scn 25 25 40 40 re f\n"); }

    /// The content of the cell of the pattern /P2 of the tiling pattern documents: a blue mark
    /// [0 0 5 5] in the lower left corner of the cell 20 x 20; the rest of the cell is not painted
    static inline QByteArray tilingPhasePatternContent() { return QByteArray("0 0 1 rg 0 0 5 5 re f"); }

    /// The content of the cell of the pattern /P3 of the tiling pattern documents: a red mark
    /// [0 0 10 10] in the lower left corner of the cell 40 x 40; the rest of the cell is not painted
    static inline QByteArray tilingMarkPatternContent() { return QByteArray("1 0 0 rg 0 0 10 10 re f"); }

    /// The content of the cell of the pattern /P7 of the tiling pattern documents, a wrong
    /// content: its first operator rg has two operands instead of three (it fails, the fill
    /// color of the cell stays the initial black), then the square [0 0 10 10] is filled, and the
    /// three operands 7 8 9 are left without an operator at its end. A cell is a content stream
    /// of its own: neither an operand of the page nor the three numbers of the previous cell
    /// complete its rg.
    static inline QByteArray tilingStrayOperandsPatternContent() { return QByteArray("0 0 rg 0 0 10 10 re f 7 8 9"); }

    /// The content of the form /FmP of the tiling pattern documents: the square [12 12 42 42] of
    /// the form space filled by the pattern /P2 of the resources of the form
    static inline QByteArray tilingPatternFormContent() { return QByteArray("/Pattern cs /P2 scn 12 12 30 30 re f"); }

    /// The resources of the tiling pattern documents: /Font << /F1 (addStandardFontObject,
    /// Helvetica) >> and /Pattern << /P1 /P2 /P3 >>, three colored tiling patterns (ISO 32000-2,
    /// 8.7.3; /PatternType 1, /PaintType 1, /TilingType 1), each an indirect stream:
    ///
    ///   /P1  /BBox [0 0 10 10], /XStep 20, /YStep 20, /Matrix [1 0 0 1 5 5], /Resources << /ExtGState
    ///        << /GT << /Type /ExtGState /LW 2 >> >> >>, the content tilingPatternContent. Its cells
    ///        are at (5 + 20 i, 5 + 20 j) of the page, each a whole red square 10 x 10.
    ///   /P2  /BBox [0 0 20 20], /XStep 20, /YStep 20, no /Matrix (the pattern space is the default
    ///        space of the page), /Resources << >>, the content tilingPhasePatternContent. Its
    ///        cells are at (20 i, 20 j) of the page; each one shows the blue mark [20 i, 20 j,
    ///        20 i + 5, 20 j + 5] - the marks show the phase of the lattice, wherever the painted
    ///        area begins.
    ///   /P3  /BBox [0 0 40 40], /XStep 40, /YStep 40, /Matrix [1 0 0 1 10 10], /Resources << >>, the
    ///        content tilingMarkPatternContent. Its cells are at (10 + 40 i, 10 + 40 j) of the
    ///        page; each one shows the red mark 10 x 10 in its lower left corner.
    ///   /P4  as /P2 with /Matrix [1 0 0 1 7 3], a translation, which is not a multiple of the
    ///        step: the blue marks are [7 + 20 i, 3 + 20 j, 12 + 20 i, 8 + 20 j] of the page.
    ///   /P5  as /P4 with /XStep -20 and /YStep -20: a negative step only reverses the direction
    ///        of the lattice, the marks are those of /P4.
    ///   /P6  as /P2 with /Matrix [0 1 -1 0 100 0], a rotation by 90 degrees: the point (x, y) of
    ///        the pattern space is (100 - y, x) of the page, so the mark of the cell (i, j) is
    ///        [95 - 20 j, 20 i, 100 - 20 j, 20 i + 5] of the page.
    ///   /P7  /BBox [0 0 10 10], /XStep 20, /YStep 20, no /Matrix, the content
    ///        tilingStrayOperandsPatternContent (a wrong content, for the operand stack).
    ///
    /// and /XObject << /FmP >>, a form with /BBox [0 0 100 100], /Matrix [2 0 0 2 10 20], /Resources
    /// << /Pattern << /P2 (the same pattern object) >> >> and the content tilingPatternFormContent.
    /// The pattern is a resource of the form, so its pattern space is the space of the form (ISO
    /// 32000-2, 8.7.3.1: the form space at the time the form is painted by Do): drawn by "/FmP Do"
    /// with the identity as the current transformation matrix, the point (x, y) of the pattern
    /// space is (2 x + 10, 2 y + 20) of the page and the mark of the cell (i, j) is the square
    /// 10 x 10 at (10 + 40 i, 20 + 40 j) of the page.
    static inline pdf::PDFDictionaryBuilder createTilingPatternResources(pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject graphicState = createDictionaryObject({ { "Type", pdf::PDFObject::createName("ExtGState") },
                                                               { "LW", pdf::PDFObject::createInteger(2) } });
        pdf::PDFObject patternResources = createDictionaryObject({ { "ExtGState", createDictionaryObject({ { "GT", std::move(graphicState) } }) } });

        pdf::PDFObject pattern1 = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("Pattern") },
                                                             { "PatternType", pdf::PDFObject::createInteger(1) },
                                                             { "PaintType", pdf::PDFObject::createInteger(1) },
                                                             { "TilingType", pdf::PDFObject::createInteger(1) },
                                                             { "BBox", createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                             { "XStep", pdf::PDFObject::createInteger(20) },
                                                             { "YStep", pdf::PDFObject::createInteger(20) },
                                                             { "Matrix", createIntegerArrayObject({ 1, 0, 0, 1, 5, 5 }) },
                                                             { "Resources", std::move(patternResources) } }, tilingPatternContent());

        pdf::PDFObject pattern2 = addTilingPatternObject(builder, 20, 20, 20, { }, tilingPhasePatternContent());
        pdf::PDFObject pattern3 = addTilingPatternObject(builder, 40, 40, 40, { 1, 0, 0, 1, 10, 10 }, tilingMarkPatternContent());
        pdf::PDFObject pattern4 = addTilingPatternObject(builder, 20, 20, 20, { 1, 0, 0, 1, 7, 3 }, tilingPhasePatternContent());
        pdf::PDFObject pattern5 = addTilingPatternObject(builder, 20, -20, -20, { 1, 0, 0, 1, 7, 3 }, tilingPhasePatternContent());
        pdf::PDFObject pattern6 = addTilingPatternObject(builder, 20, 20, 20, { 0, 1, -1, 0, 100, 0 }, tilingPhasePatternContent());
        pdf::PDFObject pattern7 = addTilingPatternObject(builder, 10, 20, 20, { }, tilingStrayOperandsPatternContent());

        // The form /FmP: the pattern /P2 is a resource of the form
        pdf::PDFObject formResources = createDictionaryObject({ { "Pattern", createDictionaryObject({ { "P2", pattern2 } }) } });
        pdf::PDFObject form = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                         { "Subtype", pdf::PDFObject::createName("Form") },
                                                         { "BBox", createIntegerArrayObject({ 0, 0, 100, 100 }) },
                                                         { "Matrix", createIntegerArrayObject({ 2, 0, 0, 2, 10, 20 }) },
                                                         { "Resources", std::move(formResources) } }, tilingPatternFormContent());

        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), createDictionaryObject({ { "F1", addStandardFontObject(builder, "Helvetica") } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Pattern"), createDictionaryObject({ { "P1", std::move(pattern1) },
                                                                                              { "P2", std::move(pattern2) },
                                                                                              { "P3", std::move(pattern3) },
                                                                                              { "P4", std::move(pattern4) },
                                                                                              { "P5", std::move(pattern5) },
                                                                                              { "P6", std::move(pattern6) },
                                                                                              { "P7", std::move(pattern7) } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "FmP", std::move(form) } }));
        return resources;
    }

    /// Adds a colored tiling pattern (/PatternType 1, /PaintType 1, /TilingType 1) with /BBox
    /// [0 0 boxSize boxSize], the steps, /Resources << >> and the content of its cell; returns
    /// the reference to it
    /// \param matrix The six numbers of /Matrix, empty = no /Matrix
    static inline pdf::PDFObject addTilingPatternObject(pdf::PDFDocumentBuilder* builder,
                                                        pdf::PDFInteger boxSize,
                                                        pdf::PDFInteger xStep,
                                                        pdf::PDFInteger yStep,
                                                        std::vector<pdf::PDFInteger> matrix,
                                                        QByteArray content)
    {
        DictionaryEntries entries = { { "Type", pdf::PDFObject::createName("Pattern") },
                                      { "PatternType", pdf::PDFObject::createInteger(1) },
                                      { "PaintType", pdf::PDFObject::createInteger(1) },
                                      { "TilingType", pdf::PDFObject::createInteger(1) },
                                      { "BBox", createIntegerArrayObject({ 0, 0, boxSize, boxSize }) },
                                      { "XStep", pdf::PDFObject::createInteger(xStep) },
                                      { "YStep", pdf::PDFObject::createInteger(yStep) },
                                      { "Resources", createDictionaryObject(DictionaryEntries()) } };
        if (!matrix.empty())
        {
            entries.emplace_back("Matrix", createIntegerArrayObject(std::move(matrix)));
        }

        return addStreamObject(builder, std::move(entries), std::move(content));
    }

    /// A page with \p pageContent and the resources of createTilingPatternResources (/P1, /P2,
    /// /P3, /F1)
    static inline pdf::PDFDocument createTilingPatternDocument(QByteArray pageContent)
    {
        return createDocument(std::move(pageContent), &ContentTestDocuments::createTilingPatternResources);
    }

    /// A page filling a square with the colored tiling pattern /P1: the content
    /// tilingPatternPageContent. The cells of /P1 are at (5 + 20 i, 5 + 20 j) of the page, each
    /// 10 x 10: the filled square x 25 ... 65, y 25 ... 65 shows four whole red cells with the lower
    /// left corners (25, 25), (45, 25), (25, 45), (45, 45); the rest of the square is not painted.
    static inline pdf::PDFDocument createTilingPatternDocument()
    {
        return createTilingPatternDocument(tilingPatternPageContent());
    }

    // ---------------------------------------------------------------------------------------
    //  Documents of the round trip (the gate of stage E0, UnitTestsContentRoundTrip)
    // ---------------------------------------------------------------------------------------

    /// The eight orientations of a 2 x 2 matrix with the entries 0, 1, -1 (four rotations, not
    /// mirrored and mirrored), each placed in a cell 50 x 100 of the page: the columns x 0, 50, 100,
    /// 150, the rows y 0 and 100. "a b c d" of the cell i are orientationEntries()[4 i ... 4 i + 3]:
    ///   1 0 0 1 (upright), -1 0 0 1 (mirrored left to right), 1 0 0 -1 (mirrored top to bottom),
    ///   -1 0 0 -1 (turned by 180 degrees), 0 1 -1 0 (turned by 90 degrees), 0 -1 1 0 (turned by
    ///   270 degrees), 0 1 1 0 and 0 -1 -1 0 (turned and mirrored).
    /// The old bug reports GE-413, GE-410 and GE-211 (zadani.md 4.5) were images and texts turned
    /// or mirrored after an edit.
    static inline std::vector<int> orientationEntries() { return { 1, 0, 0, 1, -1, 0, 0, 1, 1, 0, 0, -1, -1, 0, 0, -1, 0, 1, -1, 0, 0, -1, 1, 0, 0, 1, 1, 0, 0, -1, -1, 0 }; }

    /// The samples of the image /Im1 of createImageOrientationsDocument: 2 x 2 pixels of
    /// DeviceRGB, 8 bits per component; the upper row red and green, the lower row blue and
    /// yellow (ISO 32000-2, 8.9.5.2: the first sample is the upper left corner of the unit square).
    /// No two pixels are equal and no pixel is white, so every orientation is distinguishable.
    static inline QByteArray orientationImageData() { return QByteArray("\xFF\x00\x00" "\x00\xFF\x00" "\x00\x00\xFF" "\xFF\xFF\x00", 12); }

    /// The content of createImageOrientationsDocument: the image in the eight orientations, the
    /// unit square scaled to 40 x 20 (a b c d = 40 x the first and 20 x the second column of the
    /// orientation), e and f chosen so that the box of the image starts 5 points inside its cell
    /// (x 5 ... 45, y 5 ... 25 for the upright one, x 5 ... 25, y 105 ... 145 for the turned ones).
    static inline QByteArray imageOrientationsPageContent()
    {
        return QByteArray("q 40 0 0 20 5 5 cm /Im1 Do Q\n"
                          "q -40 0 0 20 95 5 cm /Im1 Do Q\n"
                          "q 40 0 0 -20 105 25 cm /Im1 Do Q\n"
                          "q -40 0 0 -20 195 25 cm /Im1 Do Q\n"
                          "q 0 40 -20 0 25 105 cm /Im1 Do Q\n"
                          "q 0 -40 20 0 55 145 cm /Im1 Do Q\n"
                          "q 0 40 20 0 105 105 cm /Im1 Do Q\n"
                          "q 0 -40 -20 0 175 145 cm /Im1 Do Q\n");
    }

    /// A page with the image of orientationImageData in the eight orientations
    /// (imageOrientationsPageContent); /XObject << /Im1 >>, an indirect image XObject /Width 2
    /// /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8, no filter.
    static inline pdf::PDFDocument createImageOrientationsDocument()
    {
        return createDocument(imageOrientationsPageContent(), [](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFObject image = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                              { "Subtype", pdf::PDFObject::createName("Image") },
                                                              { "Width", pdf::PDFObject::createInteger(2) },
                                                              { "Height", pdf::PDFObject::createInteger(2) },
                                                              { "ColorSpace", pdf::PDFObject::createName("DeviceRGB") },
                                                              { "BitsPerComponent", pdf::PDFObject::createInteger(8) } }, orientationImageData());
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Im1", std::move(image) } }));
            return resources;
        });
    }

    /// The glyph procedure of the code 70 (/F) of the Type 3 font of createTextOrientationsDocument:
    /// the letter F of three rectangles of the glyph space - the stem [0 0 20 100], the upper arm
    /// [20 80 80 100] and the shorter middle arm [20 40 60 60]. No turn or mirror maps it onto itself.
    static inline QByteArray orientationGlyphProcedure() { return QByteArray("100 0 d0 0 0 20 100 re f 20 80 60 20 re f 20 40 40 20 re f"); }

    /// The content of createTextOrientationsDocument: the letter F of the Type 3 font /T3 at the
    /// size 40 (the glyph box [0 0 80 100] of the glyph space is [0 0 32 40] of the text space) in
    /// the eight orientations as the text matrix Tm; e and f put the glyph 5 points inside its
    /// cell (x 5 ... 37, y 5 ... 45 for the upright one).
    static inline QByteArray textOrientationsPageContent()
    {
        return QByteArray("BT /T3 40 Tf 1 0 0 1 5 5 Tm (F) Tj ET\n"
                          "BT /T3 40 Tf -1 0 0 1 87 5 Tm (F) Tj ET\n"
                          "BT /T3 40 Tf 1 0 0 -1 105 45 Tm (F) Tj ET\n"
                          "BT /T3 40 Tf -1 0 0 -1 187 45 Tm (F) Tj ET\n"
                          "BT /T3 40 Tf 0 1 -1 0 45 105 Tm (F) Tj ET\n"
                          "BT /T3 40 Tf 0 -1 1 0 55 137 Tm (F) Tj ET\n"
                          "BT /T3 40 Tf 0 1 1 0 105 105 Tm (F) Tj ET\n"
                          "BT /T3 40 Tf 0 -1 -1 0 195 137 Tm (F) Tj ET\n");
    }

    /// A page with the letter F in the eight orientations (textOrientationsPageContent). /Font
    /// << /T3 >> is a Type 3 font written by hand, so the rendering depends on no font of the
    /// system: /FontMatrix [0.01 0 0 0.01 0 0], /FontBBox [0 0 100 100], /FirstChar 70, /LastChar
    /// 70, /Widths [100], /Encoding << /Type /Encoding /Differences [70 /F] >>, /CharProcs << /F
    /// (orientationGlyphProcedure) >>, /Resources << >>.
    static inline pdf::PDFDocument createTextOrientationsDocument()
    {
        return createDocument(textOrientationsPageContent(), [](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFObject font = addObject(builder, createDictionaryObject({ { "Type", pdf::PDFObject::createName("Font") },
                                                                              { "Subtype", pdf::PDFObject::createName("Type3") },
                                                                              { "FontMatrix", createNumberArrayObject({ TYPE3_FONT_MATRIX_SCALE, 0, 0, TYPE3_FONT_MATRIX_SCALE, 0, 0 }) },
                                                                              { "FontBBox", createIntegerArrayObject({ 0, 0, 100, 100 }) },
                                                                              { "FirstChar", pdf::PDFObject::createInteger(70) },
                                                                              { "LastChar", pdf::PDFObject::createInteger(70) },
                                                                              { "Widths", createIntegerArrayObject({ 100 }) },
                                                                              { "Encoding", createDictionaryObject({ { "Type", pdf::PDFObject::createName("Encoding") },
                                                                                                                     { "Differences", createArrayObject({ pdf::PDFObject::createInteger(70), pdf::PDFObject::createName("F") }) } }) },
                                                                              { "CharProcs", createDictionaryObject({ { "F", addStreamObject(builder, DictionaryEntries(), orientationGlyphProcedure()) } }) },
                                                                              { "Resources", createDictionaryObject(DictionaryEntries()) } }));
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), createDictionaryObject({ { "T3", std::move(font) } }));
            return resources;
        });
    }

    /// The streams of /Contents of createSplitOperationDocument (ISO 32000-2, 7.8.2: the division
    /// may be anywhere between two lexical tokens): the operands of re and of rg are split
    /// between two streams, the stream 2 is empty, the last one holds the painting operator
    /// alone. Joined by one line feed they are "1 0 0 rg 10 10\n 50 50 re f\n\n\n0 0 1\n rg 100
    /// 100 20 20 re\n\nf": a red square [10 10 60 60] and a blue square [100 100 120 120].
    static inline QList<QByteArray> splitOperationStreams() { return { QByteArray("1 0 0 rg 10 10"), QByteArray(" 50 50 re f\n"), QByteArray(), QByteArray("0 0 1"), QByteArray(" rg 100 100 20 20 re\n"), QByteArray("f") }; }

    /// A page, whose content is split over six streams in the middle of operations
    /// (splitOperationStreams); no resources
    static inline pdf::PDFDocument createSplitOperationDocument()
    {
        return createDocument(splitOperationStreams(), &ContentTestDocuments::createNoResources);
    }

    /// The shared content stream of the pages 2 and 3 of createPageTreeDocument: a blue square
    /// [10 10 40 40], then the form /FmA moved by (50, 50)
    static inline QByteArray pageTreeSharedContent() { return QByteArray("0 0 1 rg 10 10 30 30 re f q 1 0 0 1 50 50 cm /FmA Do Q"); }

    /// The content of the page 4 of createPageTreeDocument: the form /FmA moved by (100, 100),
    /// then the form /FmB
    static inline QByteArray pageTreeOwnContent() { return QByteArray("q 1 0 0 1 100 100 cm /FmA Do Q /FmB Do"); }

    /// A document of four pages, which share objects:
    ///   page 1  no /Contents (nothing is painted) and no /Resources;
    ///   page 2  /Contents is the stream of pageTreeSharedContent, no /Resources;
    ///   page 3  /Contents is the same stream object as of the page 2, no /Resources;
    ///   page 4  its own stream pageTreeOwnContent and its own /Resources << /XObject << /FmA /FmB >> >>.
    /// The page tree root has /Resources << /XObject << /FmA >> /ProcSet [/PDF] >>, which the
    /// pages 1 - 3 inherit (ISO 32000-2, 7.7.3.4). Two forms, each an indirect stream:
    ///   /FmA  /BBox [0 0 60 30], /Resources << /XObject << /FmB >> >>, the content "1 0 0 rg 0 0
    ///         20 20 re f /FmB Do": a red square and the form /FmB inside of the form;
    ///   /FmB  /BBox [0 0 20 20], /Matrix [1 0 0 1 30 0], no /Resources, the content "0 1 0 rg
    ///         5 5 10 10 re f": a green square [35 5 45 15] of the space it is painted in.
    /// /FmA is used by three pages, /FmB by the form /FmA and by the page 4.
    static inline pdf::PDFDocument createPageTreeDocument()
    {
        pdf::PDFDocumentBuilder builder;
        pdf::PDFObject formB = addStreamObject(&builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                           { "Subtype", pdf::PDFObject::createName("Form") },
                                                           { "BBox", createIntegerArrayObject({ 0, 0, 20, 20 }) },
                                                           { "Matrix", createIntegerArrayObject({ 1, 0, 0, 1, 30, 0 }) } }, QByteArray("0 1 0 rg 5 5 10 10 re f"));
        pdf::PDFObject formA = addStreamObject(&builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                           { "Subtype", pdf::PDFObject::createName("Form") },
                                                           { "BBox", createIntegerArrayObject({ 0, 0, 60, 30 }) },
                                                           { "Resources", createDictionaryObject({ { "XObject", createDictionaryObject({ { "FmB", formB } }) } }) } }, QByteArray("1 0 0 rg 0 0 20 20 re f /FmB Do"));
        pdf::PDFObject sharedContents = addStreamObject(&builder, DictionaryEntries(), pageTreeSharedContent());

        builder.appendPage(QRectF(0, 0, PAGE_SIZE, PAGE_SIZE));
        for (int i = 0; i < 2; ++i)
        {
            builder.mergeTo(builder.appendPage(QRectF(0, 0, PAGE_SIZE, PAGE_SIZE)), createDictionaryObject({ { "Contents", sharedContents } }));
        }
        builder.mergeTo(builder.appendPage(QRectF(0, 0, PAGE_SIZE, PAGE_SIZE)),
                        createDictionaryObject({ { "Contents", addStreamObject(&builder, DictionaryEntries(), pageTreeOwnContent()) },
                                                 { "Resources", createDictionaryObject({ { "XObject", createDictionaryObject({ { "FmA", formA }, { "FmB", formB } }) } }) } }));

        const pdf::PDFObject& catalog = builder.getObjectByReference(builder.getCatalogReference());
        const pdf::PDFObjectReference pageTreeRoot = catalog.getDictionary()->get("Pages").getReference();
        builder.mergeTo(pageTreeRoot, createDictionaryObject({ { "Resources", createDictionaryObject({ { "XObject", createDictionaryObject({ { "FmA", formA } }) },
                                                                                                       { "ProcSet", createArrayObject({ pdf::PDFObject::createName("PDF") }) } }) } }));
        return builder.build();
    }

    /// The content of createTwoTilingPatternsDocument: the square [10 10 90 90] filled by the
    /// uncolored pattern /PU in dark green, the square [110 10 190 90] filled by the colored
    /// pattern /PC, the rectangle [20 120 180 180] stroked by /PU in red with the line width 8
    static inline QByteArray twoTilingPatternsPageContent()
    {
        return QByteArray("/CsP cs 0 0.5 0 /PU scn 10 10 80 80 re f\n"
                          "/Pattern cs /PC scn 110 10 80 80 re f\n"
                          "/CsP CS 1 0 0 /PU SCN 8 w 20 120 160 60 re S\n");
    }

    /// A page filling and stroking with a colored and an uncolored tiling pattern (ISO 32000-2,
    /// 8.7.3.2 and 8.7.3.3; twoTilingPatternsPageContent). The resources: /ColorSpace << /CsP
    /// [/Pattern /DeviceRGB] >> (the underlying space of the uncolored pattern) and /Pattern <<
    /// /PU /PC >>, both indirect streams /PatternType 1 /TilingType 1 /BBox [0 0 10 10] /XStep 10
    /// /YStep 10 /Resources << >>:
    ///   /PU  /PaintType 2 (uncolored), /Matrix [1 0 0 1 3 3], the cell "0 0 6 6 re f" - a square
    ///        of the color of the use;
    ///   /PC  /PaintType 1 (colored), no /Matrix, the cell "1 0 0 rg 0 0 5 5 re f 0 0 1 rg 5 5 5
    ///        5 re f" - a red and a blue square.
    static inline pdf::PDFDocument createTwoTilingPatternsDocument()
    {
        return createDocument(twoTilingPatternsPageContent(), [](pdf::PDFDocumentBuilder* builder)
        {
            DictionaryEntries uncolored = { { "Type", pdf::PDFObject::createName("Pattern") },
                                            { "PatternType", pdf::PDFObject::createInteger(1) },
                                            { "PaintType", pdf::PDFObject::createInteger(2) },
                                            { "TilingType", pdf::PDFObject::createInteger(1) },
                                            { "BBox", createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                            { "XStep", pdf::PDFObject::createInteger(10) },
                                            { "YStep", pdf::PDFObject::createInteger(10) },
                                            { "Matrix", createIntegerArrayObject({ 1, 0, 0, 1, 3, 3 }) },
                                            { "Resources", createDictionaryObject(DictionaryEntries()) } };
            pdf::PDFObject patternUncolored = addStreamObject(builder, std::move(uncolored), QByteArray("0 0 6 6 re f"));
            pdf::PDFObject patternColored = addTilingPatternObject(builder, 10, 10, 10, { }, QByteArray("1 0 0 rg 0 0 5 5 re f 0 0 1 rg 5 5 5 5 re f"));

            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), createDictionaryObject({ { "CsP", createArrayObject({ pdf::PDFObject::createName("Pattern"), pdf::PDFObject::createName("DeviceRGB") }) } }));
            resources.addEntry(pdf::PDFInplaceOrMemoryString("Pattern"), createDictionaryObject({ { "PU", std::move(patternUncolored) }, { "PC", std::move(patternColored) } }));
            return resources;
        });
    }

    /// The content of createSoftMaskImageDocument: the soft mask of /GS1 is set under the identity,
    /// then the image /Im1 is drawn under the matrix [100 0 0 50 50 60] (x 50 ... 150, y 60 ... 110)
    static inline QByteArray softMaskImagePageContent() { return QByteArray("/GS1 gs q 100 0 0 50 50 60 cm /Im1 Do Q\n"); }

    /// A page with an image under a soft mask set in another space than the image (R-6, ISO
    /// 32000-2, 11.6.5.1: the mask stays where the CTM of its gs put it): softMaskImagePageContent.
    /// The resources: /ExtGState << /GS1 << /Type /ExtGState /SMask << /Type /Mask /S /Luminosity /G
    /// (form) >> >> >>, the form a transparency group as in createSoftMaskDocument (the gray 0.5 on
    /// the left half [0 0 100 200]), and /XObject << /Im1 >>, the image of
    /// createImageOrientationsDocument (2 x 2 pixels, DeviceRGB).
    static inline pdf::PDFDocument createSoftMaskImageDocument()
    {
        return createDocument(softMaskImagePageContent(), [](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFObject form = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                             { "Subtype", pdf::PDFObject::createName("Form") },
                                                             { "BBox", createIntegerArrayObject({ 0, 0, 200, 200 }) },
                                                             { "Group", createDictionaryObject({ { "S", pdf::PDFObject::createName("Transparency") },
                                                                                                { "CS", pdf::PDFObject::createName("DeviceGray") } }) } }, softMaskGroupContent());
            pdf::PDFObject softMask = createDictionaryObject({ { "Type", pdf::PDFObject::createName("Mask") },
                                                               { "S", pdf::PDFObject::createName("Luminosity") },
                                                               { "G", std::move(form) } });
            pdf::PDFObject image = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                              { "Subtype", pdf::PDFObject::createName("Image") },
                                                              { "Width", pdf::PDFObject::createInteger(2) },
                                                              { "Height", pdf::PDFObject::createInteger(2) },
                                                              { "ColorSpace", pdf::PDFObject::createName("DeviceRGB") },
                                                              { "BitsPerComponent", pdf::PDFObject::createInteger(8) } }, orientationImageData());

            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("ExtGState"), createDictionaryObject({ { "GS1", createDictionaryObject({ { "Type", pdf::PDFObject::createName("ExtGState") },
                                                                                                                                  { "SMask", std::move(softMask) } }) } }));
            resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Im1", std::move(image) } }));
            return resources;
        });
    }

    /// The content of createClippingDocument, four groups:
    ///   1  a clipping path [10 10 90 90], then the red square [0 0 100 100] painted through it;
    ///   2  a chain of three q: the first two end together (Q Q), the third later - a blue square
    ///      [115 15 145 45] (moved by the cm) and a green square [150 10 180 40] after them;
    ///   3  a path, which paints and clips (re W f): the yellow square [20 110 80 170], then the
    ///      black square [0 100 100 200] painted through it;
    ///   4  a text, which clips (7 Tr: ISO 32000-2, 9.3.6 - add to the clipping path, paint
    ///      nothing), then the magenta square [100 100 200 200] painted through the glyphs;
    ///   5  a clipping path [150 50 190 90], then the shading /Sh1 painted through it (sh paints
    ///      the whole clip region, 8.7.4.2).
    static inline QByteArray clippingPageContent()
    {
        return QByteArray("q 10 10 80 80 re W n 1 0 0 rg 0 0 100 100 re f Q\n"
                          "q q q 0 0 1 rg 1 0 0 1 5 5 cm 110 10 30 30 re f Q Q 0 1 0 rg 150 10 30 30 re f Q\n"
                          "q 1 1 0 rg 20 110 60 60 re W f 0 0 0 rg 0 100 100 100 re f Q\n"
                          "q BT /F1 48 Tf 7 Tr 110 150 Td (AB) Tj ET 1 0 1 rg 100 100 100 100 re f Q\n"
                          "q 150 50 40 40 re W n /Sh1 sh Q\n");
    }

    /// A page with clipping paths, a chain of q, a clipping text and a shading
    /// (clippingPageContent). The resources: /Font << /F1 (addStandardFontObject, Helvetica) >>,
    /// /Shading << /Sh1 << /ShadingType 2 /ColorSpace /DeviceRGB /Coords [150 50 190 90] /Function
    /// << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >> >> >> (an axial shading from
    /// red to blue) and /ColorSpace << /DefaultRGB [/CalRGB << /WhitePoint [0.9505 1 1.089] >>] >>
    /// - a default space, which no operator names (ISO 32000-2, 8.6.5.6: rg paints in it); the
    /// entry must survive every rewrite (R-4).
    static inline pdf::PDFDocument createClippingDocument()
    {
        return createDocument(clippingPageContent(), [](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFObject calRGB = createArrayObject({ pdf::PDFObject::createName("CalRGB"),
                                                        createDictionaryObject({ { "WhitePoint", createNumberArrayObject({ 0.9505, 1.0, 1.089 }) } }) });
            pdf::PDFObject function = createDictionaryObject({ { "FunctionType", pdf::PDFObject::createInteger(2) },
                                                               { "Domain", createIntegerArrayObject({ 0, 1 }) },
                                                               { "C0", createIntegerArrayObject({ 1, 0, 0 }) },
                                                               { "C1", createIntegerArrayObject({ 0, 0, 1 }) },
                                                               { "N", pdf::PDFObject::createInteger(1) } });
            pdf::PDFObject shading = createDictionaryObject({ { "ShadingType", pdf::PDFObject::createInteger(2) },
                                                              { "ColorSpace", pdf::PDFObject::createName("DeviceRGB") },
                                                              { "Coords", createIntegerArrayObject({ 150, 50, 190, 90 }) },
                                                              { "Function", std::move(function) } });
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), createDictionaryObject({ { "F1", addStandardFontObject(builder, "Helvetica") } }));
            resources.addEntry(pdf::PDFInplaceOrMemoryString("Shading"), createDictionaryObject({ { "Sh1", std::move(shading) } }));
            resources.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), createDictionaryObject({ { "DefaultRGB", std::move(calRGB) } }));
            return resources;
        });
    }
};

}   // namespace pdftest

#endif // CONTENTTESTDOCUMENTS_H
