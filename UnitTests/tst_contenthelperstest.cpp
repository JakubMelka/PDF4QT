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

// The shared helpers of the core, which the content editing is built on:
//  - PDFContentStreamText: the one format of the numbers, names and strings of a content stream
//    (ISO 32000-2, 7.2.3, 7.3.3, 7.3.4, 7.3.5) and its callers of the redaction and of the text
//    layer of OCR;
//  - PDFImagePageFactory::createImageXObject: the image XObject of a frame (8.9.5, Table 87;
//    the soft mask of 11.6.5.2, Table 143; the profile of 8.6.5.5, Table 65);
//  - the parameters of the functions of the types 2 and 3 (7.10.3, 7.10.4, Tables 38, 40, 41);
//  - the texts of /ToUnicode of more code units (9.10.3).
// The expected values are written by hand from the standard, never taken from a run.

#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfconstants.h"
#include "pdfcontentstreamtext.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdffunction.h"
#include "pdfimagepagefactory.h"
#include "pdfmeshqualitysettings.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfoptionalcontent.h"
#include "pdfparser.h"
#include "pdfredact.h"

#include <QtTest>
#include <QColor>
#include <QColorSpace>
#include <QImage>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

using namespace pdf;

using RealVector = std::vector<PDFReal>;

/// A font created from a font dictionary with a /ToUnicode stream, and the document of the stream
struct ContentHelpersFont
{
    PDFDocument document;
    PDFFontPointer font;
};

/// The objects created for an image of a frame
struct ContentHelpersImage
{
    PDFObjectReference reference;           ///< The image XObject
    const PDFStream* stream = nullptr;      ///< Its stream
    const PDFDictionary* dictionary = nullptr;
    size_t objectCountBefore = 0;           ///< Objects of the builder before the image was created
    size_t objectCountAfter = 0;
};

class ContentHelpersTest : public QObject
{
    Q_OBJECT

private slots:
    // PDFContentStreamText
    void formatNumber_data();
    void formatNumber();
    void formatNumberSinglePrecision_data();
    void formatNumberSinglePrecision();
    void formatNumberNotFinite();
    void isNumberWritable();
    void formatFixedNumber_data();
    void formatFixedNumber();
    void redactFillNumbers();
    void formatName_data();
    void formatName();
    void formatLiteralString_data();
    void formatLiteralString();
    void formatHexString_data();
    void formatHexString();
    void formatString_data();
    void formatString();
    void isSeparatorNeeded_data();
    void isSeparatorNeeded();

    // PDFImagePageFactory::createImageXObject
    void imageXObjectJpeg_data();
    void imageXObjectJpeg();
    void imageXObjectLossless_data();
    void imageXObjectLossless();
    void imageXObjectSoftMask();
    void imageXObjectOpaqueAlpha();
    void imageXObjectCompactedFrame();
    void imageXObjectIccProfile();
    void imageXObjectIccProfileOfOtherComponents();
    void imageXObjectEmptyFrame();
    void imagePageUsesImageXObject();
    void imagePageChecksEmptyFrameFirst();

    // Parameters of the functions
    void exponentialFunctionParameters();
    void exponentialFunctionDefaults();
    void stitchingFunctionParameters();
    void stitchingFunctionOfOneFunction();

    // /ToUnicode of more code units
    void toUnicodeText_data();
    void toUnicodeText();
    void toUnicodeTextRangePastLastByte_data();
    void toUnicodeTextRangePastLastByte();
    void toUnicodeTextCodeLength();
    void toUnicodeTextLongestDestination();
    void toUnicodeTextCountLimit();
    void toUnicodeTextSerialization();
    void toUnicodeTextSimpleFont_data();
    void toUnicodeTextSimpleFont();
    void toUnicodeTextSimpleFontCodeLength();
    void toUnicodeTextType0Font();
    void toUnicodeTextType0FontCodeLength();

    // The widths of the font dictionaries: "no width" against the width 0
    void fontWidthAccessors_data();
    void fontWidthAccessors();

private:
    /// The text of a character outside of the basic multilingual plane (its surrogate pair)
    static QString fromCodePoint(char32_t codePoint);

    /// Number of the significant digits of a number text
    static int getSignificantDigitCount(const QByteArray& text);

    /// The object of a text of the PDF syntax (the parser of the library reads it)
    static PDFObject parseObject(const QByteArray& text);

    /// The frame of a decoded image
    static PDFImagePageFactory::Frame createFrame(const QImage& image);

    /// A frame of a JPEG, which is written unchanged; the bytes are not decoded by the tested function
    static PDFImagePageFactory::Frame createJpegFrame(int components, bool isAdobeCmyk);

    /// Creates the image XObject of the frame in the builder and reads it back
    static ContentHelpersImage createImage(PDFDocumentBuilder* builder, const PDFImagePageFactory::Frame& frame);

    /// The value of a name entry of a dictionary (empty, when it is not a name)
    static QByteArray getName(const PDFDocumentBuilder& builder, const PDFDictionary* dictionary, const char* key);

    /// The value of an integer entry of a dictionary (-1, when it is not an integer)
    static PDFInteger getInteger(const PDFDocumentBuilder& builder, const PDFDictionary* dictionary, const char* key);

    /// The function of a text of a function dictionary
    static PDFFunctionPtr createFunction(const QByteArray& text);

    /// The ToUnicode CMap of the two-byte codes used by the tests of the texts
    static QByteArray getToUnicodeCMap();

    /// The ToUnicode CMap of ISO 32000-2, 9.10.3, EXAMPLE 2 (the content of its stream)
    static QByteArray getStandardToUnicodeCMap();

    /// A font of a font dictionary with the entries and with /ToUnicode of the CMap
    static ContentHelpersFont createFont(const QByteArray& fontEntries, const QByteArray& toUnicodeCMap);
};

QString ContentHelpersTest::fromCodePoint(char32_t codePoint)
{
    return QString::fromUcs4(&codePoint, 1);
}

int ContentHelpersTest::getSignificantDigitCount(const QByteArray& text)
{
    int count = 0;
    for (const char character : text)
    {
        if (character >= '0' && character <= '9' && (count > 0 || character != '0'))
        {
            ++count;
        }
    }
    return count;
}

PDFObject ContentHelpersTest::parseObject(const QByteArray& text)
{
    PDFParser parser(text, nullptr, PDFParser::None);
    return parser.getObject();
}

PDFImagePageFactory::Frame ContentHelpersTest::createFrame(const QImage& image)
{
    QString error;
    std::optional<PDFImagePageFactory::Frame> frame = PDFImagePageFactory::createFrame(image, 0.0, &error);
    return frame ? *frame : PDFImagePageFactory::Frame();
}

PDFImagePageFactory::Frame ContentHelpersTest::createJpegFrame(int components, bool isAdobeCmyk)
{
    PDFImagePageFactory::Frame frame;
    frame.jpegData = QByteArray("\xFF\xD8 the bytes of a JPEG file, written as they are \x00\x01\xFE \xFF\xD9", 55);
    frame.jpegSize = QSize(7, 5);
    frame.size = QSize(7, 5);
    frame.jpegComponents = components;
    frame.isAdobeCmyk = isAdobeCmyk;
    return frame;
}

ContentHelpersImage ContentHelpersTest::createImage(PDFDocumentBuilder* builder, const PDFImagePageFactory::Frame& frame)
{
    ContentHelpersImage image;
    image.objectCountBefore = builder->getStorage()->getObjects().size();
    image.reference = PDFImagePageFactory::createImageXObject(builder, frame);
    image.objectCountAfter = builder->getStorage()->getObjects().size();

    const PDFObject& object = builder->getObjectByReference(image.reference);
    if (object.isStream())
    {
        image.stream = object.getStream();
        image.dictionary = image.stream->getDictionary();
    }
    return image;
}

QByteArray ContentHelpersTest::getName(const PDFDocumentBuilder& builder, const PDFDictionary* dictionary, const char* key)
{
    const PDFObject& object = builder.getObject(dictionary->get(key));
    return object.isName() ? object.getString() : QByteArray();
}

PDFInteger ContentHelpersTest::getInteger(const PDFDocumentBuilder& builder, const PDFDictionary* dictionary, const char* key)
{
    const PDFObject& object = builder.getObject(dictionary->get(key));
    return object.isInt() ? object.getInteger() : -1;
}

PDFFunctionPtr ContentHelpersTest::createFunction(const QByteArray& text)
{
    const PDFDocument document;
    return PDFFunction::createFunction(&document, parseObject(text));
}

QByteArray ContentHelpersTest::getToUnicodeCMap()
{
    return QByteArrayLiteral(
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\n"
        "begincmap\n"
        "/CMapName /Adobe-Identity-UCS def\n"
        "/CMapType 2 def\n"
        "1 begincodespacerange\n"
        "<0000> <FFFF>\n"
        "endcodespacerange\n"
        "4 beginbfchar\n"
        "<0001> <006600660069>\n"
        "<0002> <D83DDE00>\n"
        "<0003> <0041>\n"
        "<0004> <0066006600>\n"
        "endbfchar\n"
        "4 beginbfrange\n"
        "<0010> <0012> <00660061>\n"
        "<0020> <0022> [<00660066> <00660069> <00660066006C>]\n"
        "<0030> <0032> <0041>\n"
        "<0050> <0052> <D83DDE00>\n"
        "endbfrange\n"
        "endcmap\n"
        "CMapName currentdict /CMap defineresource pop\n"
        "end\n"
        "end\n");
}

QByteArray ContentHelpersTest::getStandardToUnicodeCMap()
{
    return QByteArrayLiteral(
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\n"
        "begincmap\n"
        "/CIDSystemInfo\n"
        "<</Registry (Adobe)\n"
        "/Ordering (UCS2)\n"
        "/Supplement 0\n"
        ">> def\n"
        "/CMapName /Adobe-Identity-UCS2 def\n"
        "/CMapType 2 def\n"
        "1 begincodespacerange\n"
        "<0000> <FFFF>\n"
        "endcodespacerange\n"
        "2 beginbfrange\n"
        "<0000> <005E> <0020>\n"
        "<005F> <0061> [<00660066> <00660069> <00660066006C>]\n"
        "endbfrange\n"
        "1 beginbfchar\n"
        "<3A51> <D840DC3E>\n"
        "endbfchar\n"
        "endcmap\n"
        "CMapName currentdict /CMap defineresource pop\n"
        "end\n"
        "end\n");
}

ContentHelpersFont ContentHelpersTest::createFont(const QByteArray& fontEntries, const QByteArray& toUnicodeCMap)
{
    PDFDocumentBuilder builder;
    builder.createDocument();

    PDFDictionaryBuilder dictionary;
    dictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(toUnicodeCMap.size()));
    QByteArray content = toUnicodeCMap;
    const PDFObjectReference toUnicode = builder.addObject(PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content))));

    ContentHelpersFont result;
    result.document = builder.build();

    const QByteArray fontDictionary = "<< /Type /Font " + fontEntries + " /ToUnicode " + QByteArray::number(toUnicode.objectNumber) + " " +
                                      QByteArray::number(toUnicode.generation) + " R >>";
    result.font = PDFFont::createFont(parseObject(fontDictionary), "F1", &result.document);
    return result;
}

// ------------------------------------------------------- PDFContentStreamText

void ContentHelpersTest::formatNumber_data()
{
    QTest::addColumn<double>("value");
    QTest::addColumn<QByteArray>("expected");

    // No decimal point of an integral value, no sign of a zero
    QTest::newRow("zero") << 0.0 << QByteArray("0");
    QTest::newRow("negative zero") << -0.0 << QByteArray("0");
    QTest::newRow("one") << 1.0 << QByteArray("1");
    QTest::newRow("minus one") << -1.0 << QByteArray("-1");
    QTest::newRow("hundred") << 100.0 << QByteArray("100");
    QTest::newRow("zeros of an integral value stay") << 1200.0 << QByteArray("1200");

    // The shortest text of a short value
    QTest::newRow("half") << 0.5 << QByteArray("0.5");
    QTest::newRow("negative fraction") << -2.25 << QByteArray("-2.25");
    QTest::newRow("tenth") << 0.1 << QByteArray("0.1");

    // No exponent (ISO 32000-2, 7.3.3)
    QTest::newRow("1e-5") << 1e-5 << QByteArray("0.00001");
    QTest::newRow("-1e-5") << -1e-5 << QByteArray("-0.00001");
    QTest::newRow("1e-7") << 1e-7 << QByteArray("0.0000001");
    QTest::newRow("1e-9") << 1e-9 << QByteArray("0.000000001");
    QTest::newRow("1.5e-10") << 1.5e-10 << QByteArray("0.00000000015");
    QTest::newRow("1e15") << 1e15 << QByteArray("1000000000000000");

    // An integral part keeps its digits, whatever their count is
    QTest::newRow("nine digits") << 123456789.0 << QByteArray("123456789");
    QTest::newRow("eleven digits") << 12345678901.0 << QByteArray("12345678901");
    QTest::newRow("ten digits, fraction up") << 1234567890.75 << QByteArray("1234567891");
    QTest::newRow("ten digits, fraction down") << 1234567890.25 << QByteArray("1234567890");

    // At most nine significant digits of a value with a fraction
    QTest::newRow("third") << 1.0 / 3.0 << QByteArray("0.333333333");
    QTest::newRow("two thirds") << 2.0 / 3.0 << QByteArray("0.666666667");
    QTest::newRow("pi") << 3.14159265358979 << QByteArray("3.14159265");
    QTest::newRow("thirteen digits") << 123456.7891234 << QByteArray("123456.789");
    QTest::newRow("small, twelve digits") << 0.000123456789012 << QByteArray("0.000123456789");
    QTest::newRow("0.1 + 0.2") << 0.1 + 0.2 << QByteArray("0.3");
    QTest::newRow("single 0.1") << double(0.1f) << QByteArray("0.100000001");
    QTest::newRow("1000 + 1/16384") << 1000.00006103515625 << QByteArray("1000.00006");

    // The rounding to nine digits carries into a new digit
    QTest::newRow("carry to 10") << 9.9999999996 << QByteArray("10");
    QTest::newRow("carry to 100000000") << 99999999.96 << QByteArray("100000000");
}

void ContentHelpersTest::formatNumber()
{
    QFETCH(double, value);
    QFETCH(QByteArray, expected);

    QCOMPARE(PDFContentStreamText::formatNumber(value), expected);
    QVERIFY(PDFContentStreamText::isNumberWritable(value));
}

void ContentHelpersTest::formatNumberSinglePrecision_data()
{
    QTest::addColumn<float>("value");

    QTest::newRow("0.1") << 0.1f;
    QTest::newRow("0.7") << 0.7f;
    QTest::newRow("1/3") << 1.0f / 3.0f;
    QTest::newRow("123456.789") << 123456.789f;
    QTest::newRow("-1234.5678") << -1234.5678f;
    QTest::newRow("1e-5") << 1e-5f;
    QTest::newRow("one and the smallest step") << 1.0000001f;
    QTest::newRow("8388607.5") << 8388607.5f;
    QTest::newRow("16777216") << 16777216.0f;

    // Values, which eight significant digits do not keep: the steps of the single precision
    // values from 1000 to 1024 are 1/16384, smaller than 0.0001
    QTest::newRow("1000 + 1/16384") << 1000.00006103515625f;
    QTest::newRow("1000.178955078125") << 1000.178955078125f;
    QTest::newRow("1000.7752685546875") << 1000.7752685546875f;
    QTest::newRow("6.02214076e23") << 6.02214076e23f;
    QTest::newRow("smallest normal") << std::numeric_limits<float>::min();
    QTest::newRow("largest") << std::numeric_limits<float>::max();
    QTest::newRow("lowest") << std::numeric_limits<float>::lowest();
}

void ContentHelpersTest::formatNumberSinglePrecision()
{
    QFETCH(float, value);

    const QByteArray text = PDFContentStreamText::formatNumber(double(value));

    // The syntax of a number of a content stream: digits, at most one decimal point, no exponent;
    // the canonical text has neither a trailing zero of a fraction nor a decimal point without one
    static const QRegularExpression canonicalNumber(QStringLiteral("^-?[0-9]+(\\.[0-9]*[1-9])?$"));
    QVERIFY2(canonicalNumber.match(QString::fromLatin1(text)).hasMatch(), text.constData());

    // Nine significant digits keep every single precision value
    if (text.contains('.'))
    {
        QVERIFY2(getSignificantDigitCount(text) <= PDFContentStreamText::MAXIMUM_SIGNIFICANT_DIGITS, text.constData());
    }

    // The text reads back as the same value (exactly, not within a tolerance)
    bool ok = false;
    const double readValue = text.toDouble(&ok);
    QVERIFY2(ok, text.constData());
    QVERIFY2(float(readValue) == value, text.constData());
}

void ContentHelpersTest::formatNumberNotFinite()
{
    // PDF has neither NaN nor an infinity: the old editor writes zero and the largest single
    // precision value with the sign of the infinity (PDFDocumentWriter::getRealNumberText)
    QCOMPARE(PDFContentStreamText::formatNumber(std::numeric_limits<double>::quiet_NaN()), QByteArray("0"));

    static const QRegularExpression digits(QStringLiteral("^-?[0-9]+$"));
    const QByteArray positive = PDFContentStreamText::formatNumber(std::numeric_limits<double>::infinity());
    const QByteArray negative = PDFContentStreamText::formatNumber(-std::numeric_limits<double>::infinity());
    QVERIFY2(digits.match(QString::fromLatin1(positive)).hasMatch(), positive.constData());
    QVERIFY2(digits.match(QString::fromLatin1(negative)).hasMatch(), negative.constData());
    QVERIFY2(positive.toDouble() == double(std::numeric_limits<float>::max()), positive.constData());
    QVERIFY2(negative.toDouble() == -double(std::numeric_limits<float>::max()), negative.constData());
}

void ContentHelpersTest::isNumberWritable()
{
    QVERIFY(PDFContentStreamText::isNumberWritable(0.0));
    QVERIFY(PDFContentStreamText::isNumberWritable(-1.5));
    QVERIFY(PDFContentStreamText::isNumberWritable(std::numeric_limits<double>::max()));
    QVERIFY(PDFContentStreamText::isNumberWritable(std::numeric_limits<double>::lowest()));
    QVERIFY(PDFContentStreamText::isNumberWritable(std::numeric_limits<double>::denorm_min()));

    QVERIFY(!PDFContentStreamText::isNumberWritable(std::numeric_limits<double>::quiet_NaN()));
    QVERIFY(!PDFContentStreamText::isNumberWritable(std::numeric_limits<double>::signaling_NaN()));
    QVERIFY(!PDFContentStreamText::isNumberWritable(std::numeric_limits<double>::infinity()));
    QVERIFY(!PDFContentStreamText::isNumberWritable(-std::numeric_limits<double>::infinity()));
}

void ContentHelpersTest::formatFixedNumber_data()
{
    QTest::addColumn<double>("value");
    QTest::addColumn<int>("decimals");
    QTest::addColumn<QByteArray>("expected");

    // Four decimal places: the numbers of the fill of a redaction and of the text layer of OCR
    QTest::newRow("zero") << 0.0 << 4 << QByteArray("0");
    QTest::newRow("negative zero") << -0.0 << 4 << QByteArray("0");
    QTest::newRow("one") << 1.0 << 4 << QByteArray("1");
    QTest::newRow("minus one") << -1.0 << 4 << QByteArray("-1");
    QTest::newRow("hundred") << 100.0 << 4 << QByteArray("100");
    QTest::newRow("zeros of an integral value stay") << 1200.0 << 4 << QByteArray("1200");
    QTest::newRow("half") << 0.5 << 4 << QByteArray("0.5");
    QTest::newRow("quarter") << 0.25 << 4 << QByteArray("0.25");
    QTest::newRow("negative eighth") << -0.125 << 4 << QByteArray("-0.125");
    QTest::newRow("four places kept") << 0.1234 << 4 << QByteArray("0.1234");
    QTest::newRow("rounded up") << 1.23456789 << 4 << QByteArray("1.2346");
    QTest::newRow("rounded down") << 0.33333 << 4 << QByteArray("0.3333");
    QTest::newRow("rounded to an integer") << 0.99996 << 4 << QByteArray("1");
    QTest::newRow("rounded to a shorter fraction") << 0.50196 << 4 << QByteArray("0.502");
    QTest::newRow("large with a fraction") << 12345.67891 << 4 << QByteArray("12345.6789");
    QTest::newRow("negative with a fraction") << -612.75 << 4 << QByteArray("-612.75");

    // Tiny values of both signs: zero has no sign, the first value which is not zero is 0.0001
    QTest::newRow("0.00004") << 0.00004 << 4 << QByteArray("0");
    QTest::newRow("-0.00004") << -0.00004 << 4 << QByteArray("0");
    QTest::newRow("0.000049999") << 0.000049999 << 4 << QByteArray("0");
    QTest::newRow("-0.000049999") << -0.000049999 << 4 << QByteArray("0");
    QTest::newRow("1e-10") << 1e-10 << 4 << QByteArray("0");
    QTest::newRow("-1e-10") << -1e-10 << 4 << QByteArray("0");
    QTest::newRow("smallest positive") << std::numeric_limits<double>::denorm_min() << 4 << QByteArray("0");
    QTest::newRow("smallest negative") << -std::numeric_limits<double>::denorm_min() << 4 << QByteArray("0");
    QTest::newRow("0.00006") << 0.00006 << 4 << QByteArray("0.0001");
    QTest::newRow("-0.00006") << -0.00006 << 4 << QByteArray("-0.0001");

    // The limit of the rounding: the double precision value of 0.00005 is 0.0000500000000000000024...
    // (above the half, it is rounded up), the value before it is 0.0000499999999999999956... (zero)
    QTest::newRow("0.00005") << 0.00005 << 4 << QByteArray("0.0001");
    QTest::newRow("-0.00005") << -0.00005 << 4 << QByteArray("-0.0001");
    QTest::newRow("before 0.00005") << std::nextafter(0.00005, 0.0) << 4 << QByteArray("0");
    QTest::newRow("before -0.00005") << std::nextafter(-0.00005, 0.0) << 4 << QByteArray("0");

    // Huge values have no exponent
    QTest::newRow("1e15") << 1e15 << 4 << QByteArray("1000000000000000");
    QTest::newRow("1e20") << 1e20 << 4 << QByteArray("100000000000000000000");
    QTest::newRow("-1e20") << -1e20 << 4 << QByteArray("-100000000000000000000");

    // A value, which is not finite, is zero
    QTest::newRow("NaN") << std::numeric_limits<double>::quiet_NaN() << 4 << QByteArray("0");
    QTest::newRow("infinity") << std::numeric_limits<double>::infinity() << 4 << QByteArray("0");
    QTest::newRow("negative infinity") << -std::numeric_limits<double>::infinity() << 4 << QByteArray("0");

    // Other numbers of the decimal places
    QTest::newRow("two places") << 3.14159 << 2 << QByteArray("3.14");
    QTest::newRow("one place") << 0.1 << 1 << QByteArray("0.1");
    QTest::newRow("one place of an integral value") << 10.0 << 1 << QByteArray("10");
    QTest::newRow("no place, up") << 2.75 << 0 << QByteArray("3");
    QTest::newRow("no place, zeros stay") << 1200.4 << 0 << QByteArray("1200");
    QTest::newRow("no place, negative to zero") << -0.4 << 0 << QByteArray("0");
    QTest::newRow("negative count of places is none") << 3.14159 << -1 << QByteArray("3");
    QTest::newRow("six places") << 0.1234567 << 6 << QByteArray("0.123457");
}

void ContentHelpersTest::formatFixedNumber()
{
    QFETCH(double, value);
    QFETCH(int, decimals);
    QFETCH(QByteArray, expected);

    QCOMPARE(PDFContentStreamText::formatFixedNumber(value, decimals), expected);

    if (decimals == 4)
    {
        // The number of the text layer of OCR is the same text
        QCOMPARE(PDFOCRTextLayerWriter::formatNumber(value), expected);
    }
}

void ContentHelpersTest::redactFillNumbers()
{
    // The fill of a redacted area is the last content stream of its page: the color by rg, the
    // area as a closed path filled by f. Its numbers have at most four decimal places.
    PDFDocumentBuilder builder;
    builder.createDocument();
    builder.appendPage(QRectF(0.0, 0.0, 200.0, 200.0));
    const PDFDocument document = builder.build();

    PDFCMSGeneric cms;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::Export, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    const PDFMeshQualitySettings meshQualitySettings;
    PDFRedact redactor(&document, &fontCache, &iccProfileCache, &cms, &activity, &meshQualitySettings, QColor());

    QPainterPath path;
    path.addRect(QRectF(50.5, 100.25, 60.0, 20.0));

    PDFRedact::Settings settings;
    settings.areas = { PDFRedact::Area{ 0, path } };
    settings.fillColor = QColor(255, 128, 0);
    const PDFRedact::Result result = redactor.perform(settings, nullptr, nullptr);
    QVERIFY(!result.isStopped);
    QCOMPARE(result.document.getCatalog()->getPageCount(), size_t(1));

    const PDFObject& contents = result.document.getObject(result.document.getCatalog()->getPage(0)->getContents());
    QVERIFY(contents.isArray());
    QVERIFY(contents.getArray()->getCount() >= 2);
    const PDFObject& fill = result.document.getObject(contents.getArray()->getItem(contents.getArray()->getCount() - 1));
    QVERIFY(fill.isStream());

    const QByteArrayList lines = result.document.getDecodedStream(fill.getStream()).split('\n');
    QVERIFY2(lines.size() >= 9, lines.join('|').constData());
    QCOMPARE(lines[0], QByteArray("q"));

    // 128 / 255 = 0.50196...
    QCOMPARE(lines[1], QByteArray("1 0.502 0 rg"));
    QCOMPARE(lines[lines.size() - 4], QByteArray("h"));
    QCOMPARE(lines[lines.size() - 3], QByteArray("f"));
    QCOMPARE(lines[lines.size() - 2], QByteArray("Q"));
    QCOMPARE(lines[lines.size() - 1], QByteArray());

    // The corners of the area
    QSet<QByteArray> corners;
    for (qsizetype i = 2; i < lines.size() - 4; ++i)
    {
        const QByteArrayList tokens = lines[i].split(' ');
        QVERIFY2(tokens.size() == 3, lines[i].constData());
        QCOMPARE(tokens[2], QByteArray(i == 2 ? "m" : "l"));
        corners.insert(tokens[0] + ' ' + tokens[1]);
    }
    QCOMPARE(corners, QSet<QByteArray>({ "50.5 100.25", "110.5 100.25", "110.5 120.25", "50.5 120.25" }));
}

void ContentHelpersTest::formatName_data()
{
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<QByteArray>("expected");

    // ISO 32000-2, Table 4
    QTest::newRow("Name1") << QByteArray("Name1") << QByteArray("/Name1");
    QTest::newRow("long") << QByteArray("ASomewhatLongerName") << QByteArray("/ASomewhatLongerName");
    QTest::newRow("various characters") << QByteArray("A;Name_With-Various***Characters?") << QByteArray("/A;Name_With-Various***Characters?");
    QTest::newRow("1.2") << QByteArray("1.2") << QByteArray("/1.2");
    QTest::newRow("$$") << QByteArray("$$") << QByteArray("/$$");
    QTest::newRow("@pattern") << QByteArray("@pattern") << QByteArray("/@pattern");
    QTest::newRow(".notdef") << QByteArray(".notdef") << QByteArray("/.notdef");
    QTest::newRow("space") << QByteArray("Lime Green") << QByteArray("/Lime#20Green");
    QTest::newRow("parentheses") << QByteArray("paired()parentheses") << QByteArray("/paired#28#29parentheses");
    QTest::newRow("number sign") << QByteArray("The_Key_of_F#_Minor") << QByteArray("/The_Key_of_F#23_Minor");
    QTest::newRow("AB") << QByteArray("AB") << QByteArray("/AB");

    // 7.3.5: the solidus alone is the name of no characters
    QTest::newRow("empty") << QByteArray() << QByteArray("/");

    // 7.2.3: the delimiters and the white space are not regular characters
    QTest::newRow("delimiters") << QByteArray("a<b>c[d]e{f}g/h%i") << QByteArray("/a#3Cb#3Ec#5Bd#5De#7Bf#7Dg#2Fh#25i");
    QTest::newRow("white space") << QByteArray("a\tb\nc\fd\re") << QByteArray("/a#09b#0Ac#0Cd#0De");
    QTest::newRow("null") << QByteArray("a\0b", 3) << QByteArray("/a#00b");

    // 7.3.5: the regular characters outside of '!' ... '~'
    QTest::newRow("limits of the range") << QByteArray("!~") << QByteArray("/!~");
    QTest::newRow("control character") << QByteArray("\x01\x1F") << QByteArray("/#01#1F");
    QTest::newRow("delete") << QByteArray("\x7F") << QByteArray("/#7F");
    QTest::newRow("high bytes") << QByteArray("\x80\xFF") << QByteArray("/#80#FF");
    QTest::newRow("UTF-8") << QByteArray("caf\xC3\xA9") << QByteArray("/caf#C3#A9");
}

void ContentHelpersTest::formatName()
{
    QFETCH(QByteArray, name);
    QFETCH(QByteArray, expected);

    const QByteArray text = PDFContentStreamText::formatName(name);
    QCOMPARE(text, expected);

    // A name has no null character (7.3.5), the other names are read back by the parser
    if (!name.contains('\0'))
    {
        const PDFObject object = parseObject(text);
        QVERIFY(object.isName());
        QCOMPARE(object.getString(), name);
    }
}

void ContentHelpersTest::formatLiteralString_data()
{
    QTest::addColumn<QByteArray>("string");
    QTest::addColumn<QByteArray>("expected");

    QTest::newRow("empty") << QByteArray() << QByteArray("()");
    QTest::newRow("text") << QByteArray("This is a string") << QByteArray("(This is a string)");
    QTest::newRow("limits of the printable characters") << QByteArray(" ~") << QByteArray("( ~)");

    // ISO 32000-2, Table 3
    QTest::newRow("parentheses") << QByteArray("a(b)c") << QByteArray("(a\\(b\\)c)");
    QTest::newRow("unbalanced parenthesis") << QByteArray(")(") << QByteArray("(\\)\\()");
    QTest::newRow("reverse solidus") << QByteArray("back\\slash") << QByteArray("(back\\\\slash)");
    QTest::newRow("line feed") << QByteArray("line\nfeed") << QByteArray("(line\\nfeed)");
    QTest::newRow("carriage return") << QByteArray("\r") << QByteArray("(\\r)");
    QTest::newRow("horizontal tab") << QByteArray("\t") << QByteArray("(\\t)");
    QTest::newRow("backspace") << QByteArray("\b") << QByteArray("(\\b)");
    QTest::newRow("form feed") << QByteArray("\f") << QByteArray("(\\f)");

    // 7.3.4.2, EXAMPLE 4 and EXAMPLE 5: three octal digits
    QTest::newRow("octal characters") << QByteArray("This string contains \245two octal characters\307.")
                                      << QByteArray("(This string contains \\245two octal characters\\307.)");
    QTest::newRow("octal before a digit") << QByteArray("\005" "3") << QByteArray("(\\0053)");
    QTest::newRow("null") << QByteArray("\0", 1) << QByteArray("(\\000)");
    QTest::newRow("unit separator") << QByteArray("\x1F") << QByteArray("(\\037)");
    QTest::newRow("delete") << QByteArray("\x7F") << QByteArray("(\\177)");
    QTest::newRow("highest byte") << QByteArray("\xFF") << QByteArray("(\\377)");
}

void ContentHelpersTest::formatLiteralString()
{
    QFETCH(QByteArray, string);
    QFETCH(QByteArray, expected);

    const QByteArray text = PDFContentStreamText::formatLiteralString(string);
    QCOMPARE(text, expected);

    const PDFObject object = parseObject(text);
    QVERIFY(object.isString());
    QCOMPARE(object.getString(), string);
}

void ContentHelpersTest::formatHexString_data()
{
    QTest::addColumn<QByteArray>("string");
    QTest::addColumn<QByteArray>("expected");

    QTest::newRow("empty") << QByteArray() << QByteArray("<>");

    // ISO 32000-2, 7.3.4.3, EXAMPLE 1 and EXAMPLE 2
    QTest::newRow("text") << QByteArray("Nov shmoz ka pop.") << QByteArray("<4E6F762073686D6F7A206B6120706F702E>");
    QTest::newRow("three bytes") << QByteArray("\x90\x1F\xA3") << QByteArray("<901FA3>");

    QTest::newRow("lowest and highest byte") << QByteArray("\x00\xFF", 2) << QByteArray("<00FF>");
    QTest::newRow("upper case digits") << QByteArray("\xAB\xCD\xEF") << QByteArray("<ABCDEF>");
}

void ContentHelpersTest::formatHexString()
{
    QFETCH(QByteArray, string);
    QFETCH(QByteArray, expected);

    const QByteArray text = PDFContentStreamText::formatHexString(string);
    QCOMPARE(text, expected);

    const PDFObject object = parseObject(text);
    QVERIFY(object.isString());
    QCOMPARE(object.getString(), string);
}

void ContentHelpersTest::formatString_data()
{
    QTest::addColumn<QByteArray>("string");
    QTest::addColumn<QByteArray>("expected");

    // Every byte is printable ASCII: a literal string
    QTest::newRow("empty") << QByteArray() << QByteArray("()");
    QTest::newRow("text") << QByteArray("Hello (World)") << QByteArray("(Hello \\(World\\))");
    QTest::newRow("limits of the printable characters") << QByteArray("~ ") << QByteArray("(~ )");
    QTest::newRow("reverse solidus") << QByteArray("\\") << QByteArray("(\\\\)");

    // A byte outside of it: a hexadecimal string
    QTest::newRow("line feed") << QByteArray("a\nb") << QByteArray("<610A62>");
    QTest::newRow("unit separator") << QByteArray("\x1F") << QByteArray("<1F>");
    QTest::newRow("delete") << QByteArray("\x7F") << QByteArray("<7F>");
    QTest::newRow("UTF-16BE") << QByteArray("\xFE\xFF\x00" "A", 4) << QByteArray("<FEFF0041>");
    QTest::newRow("codes of a composite font") << QByteArray("\x00\x5F\x3A\x51", 4) << QByteArray("<005F3A51>");
}

void ContentHelpersTest::formatString()
{
    QFETCH(QByteArray, string);
    QFETCH(QByteArray, expected);

    const QByteArray text = PDFContentStreamText::formatString(string);
    QCOMPARE(text, expected);

    const PDFObject object = parseObject(text);
    QVERIFY(object.isString());
    QCOMPARE(object.getString(), string);
}

void ContentHelpersTest::isSeparatorNeeded_data()
{
    QTest::addColumn<QByteArray>("before");
    QTest::addColumn<QByteArray>("after");
    QTest::addColumn<bool>("expected");

    // Two regular characters would be read as one token
    QTest::newRow("f q") << QByteArray("f") << QByteArray("q") << true;
    QTest::newRow("Q q") << QByteArray("Q") << QByteArray("q") << true;
    QTest::newRow("number and operator") << QByteArray("10") << QByteArray("re") << true;
    QTest::newRow("two numbers") << QByteArray("1") << QByteArray("0") << true;
    QTest::newRow("number and decimal point") << QByteArray("1") << QByteArray(".5") << true;
    QTest::newRow("number and sign") << QByteArray("1") << QByteArray("-2") << true;
    QTest::newRow("name and operator") << QByteArray("/GS1") << QByteArray("gs") << true;
    QTest::newRow("operator and high byte") << QByteArray("q") << QByteArray("\x80") << true;

    // White space separates already (ISO 32000-2, Table 1)
    QTest::newRow("line feed after") << QByteArray("f") << QByteArray("\nq") << false;
    QTest::newRow("line feed before") << QByteArray("f\n") << QByteArray("q") << false;
    QTest::newRow("space before") << QByteArray("f ") << QByteArray("q") << false;
    QTest::newRow("carriage return before") << QByteArray("f\r") << QByteArray("q") << false;
    QTest::newRow("tab before") << QByteArray("f\t") << QByteArray("q") << false;
    QTest::newRow("form feed before") << QByteArray("f\f") << QByteArray("q") << false;
    QTest::newRow("null before") << QByteArray("f\0", 2) << QByteArray("q") << false;

    // A delimiter ends the token before it and is not a part of the token after it (7.2.3)
    QTest::newRow("string and operator") << QByteArray("(abc)") << QByteArray("Tj") << false;
    QTest::newRow("operator and string") << QByteArray("Tj") << QByteArray("(abc)") << false;
    QTest::newRow("hexadecimal string and operator") << QByteArray("<00>") << QByteArray("Tj") << false;
    QTest::newRow("operator and hexadecimal string") << QByteArray("ET") << QByteArray("<00>") << false;
    QTest::newRow("array and operator") << QByteArray("]") << QByteArray("TJ") << false;
    QTest::newRow("operator and array") << QByteArray("TJ") << QByteArray("[") << false;
    QTest::newRow("two names") << QByteArray("/Name") << QByteArray("/Other") << false;
    QTest::newRow("operator and name") << QByteArray("Q") << QByteArray("/GS1") << false;
    QTest::newRow("operator and comment") << QByteArray("EMC") << QByteArray("%comment") << false;
    QTest::newRow("curly bracket and operator") << QByteArray("}") << QByteArray("q") << false;
    QTest::newRow("operator and curly bracket") << QByteArray("q") << QByteArray("{") << false;

    // Nothing to separate
    QTest::newRow("empty before") << QByteArray() << QByteArray("q") << false;
    QTest::newRow("empty after") << QByteArray("q") << QByteArray() << false;
    QTest::newRow("both empty") << QByteArray() << QByteArray() << false;
}

void ContentHelpersTest::isSeparatorNeeded()
{
    QFETCH(QByteArray, before);
    QFETCH(QByteArray, after);
    QFETCH(bool, expected);

    QCOMPARE(PDFContentStreamText::isSeparatorNeeded(before, after), expected);
}

// ------------------------------------------------------- PDFImagePageFactory::createImageXObject

void ContentHelpersTest::imageXObjectJpeg_data()
{
    QTest::addColumn<int>("components");
    QTest::addColumn<bool>("isAdobeCmyk");
    QTest::addColumn<QByteArray>("colorSpace");

    QTest::newRow("gray") << 1 << false << QByteArray("DeviceGray");
    QTest::newRow("RGB") << 3 << false << QByteArray("DeviceRGB");
    QTest::newRow("CMYK") << 4 << false << QByteArray("DeviceCMYK");
    QTest::newRow("Adobe CMYK") << 4 << true << QByteArray("DeviceCMYK");
}

void ContentHelpersTest::imageXObjectJpeg()
{
    QFETCH(int, components);
    QFETCH(bool, isAdobeCmyk);
    QFETCH(QByteArray, colorSpace);

    PDFImagePageFactory::Frame frame = createJpegFrame(components, isAdobeCmyk);

    // The shown image is turned by the orientation of EXIF, the stream holds the stored pixels
    frame.size = QSize(5, 7);
    frame.transformation = QImageIOHandler::TransformationRotate90;

    PDFDocumentBuilder builder;
    builder.createDocument();
    const ContentHelpersImage image = createImage(&builder, frame);
    QVERIFY(image.dictionary);

    // One object: the image
    QCOMPARE(image.objectCountAfter, image.objectCountBefore + 1);
    QCOMPARE(size_t(image.reference.objectNumber), image.objectCountBefore);

    // ISO 32000-2, Table 87 (DCTDecode delivers samples of 8 bits) and Table 5
    QCOMPARE(getName(builder, image.dictionary, "Type"), QByteArray("XObject"));
    QCOMPARE(getName(builder, image.dictionary, "Subtype"), QByteArray("Image"));
    QCOMPARE(getInteger(builder, image.dictionary, "Width"), PDFInteger(7));
    QCOMPARE(getInteger(builder, image.dictionary, "Height"), PDFInteger(5));
    QCOMPARE(getInteger(builder, image.dictionary, "BitsPerComponent"), PDFInteger(8));
    QCOMPARE(getName(builder, image.dictionary, "ColorSpace"), colorSpace);
    QCOMPARE(getName(builder, image.dictionary, "Filter"), QByteArray("DCTDecode"));
    QCOMPARE(getInteger(builder, image.dictionary, "Length"), PDFInteger(frame.jpegData.size()));
    QVERIFY(!image.dictionary->hasKey("DecodeParms"));
    QVERIFY(!image.dictionary->hasKey("SMask"));
    QVERIFY(!image.dictionary->hasKey("Mask"));
    QVERIFY(!image.dictionary->hasKey("ImageMask"));

    // The bytes of the file unchanged
    QCOMPARE(*image.stream->getContent(), frame.jpegData);

    // Adobe writes the samples of CMYK inverted: /Decode of twice the number of the components (8.9.5.2)
    if (isAdobeCmyk)
    {
        QCOMPARE(builder.getObject(image.dictionary->get("Decode")), parseObject("[1 0 1 0 1 0 1 0]"));
    }
    else
    {
        QVERIFY(!image.dictionary->hasKey("Decode"));
    }
}

void ContentHelpersTest::imageXObjectLossless_data()
{
    QTest::addColumn<QImage>("image");
    QTest::addColumn<QByteArray>("colorSpace");
    QTest::addColumn<QByteArray>("samples");

    QImage color(2, 2, QImage::Format_RGB888);
    color.setPixelColor(0, 0, QColor(255, 0, 0));
    color.setPixelColor(1, 0, QColor(0, 255, 0));
    color.setPixelColor(0, 1, QColor(0, 0, 255));
    color.setPixelColor(1, 1, QColor(18, 52, 86));
    QTest::newRow("RGB") << color << QByteArray("DeviceRGB") << QByteArray("\xFF\x00\x00\x00\xFF\x00\x00\x00\xFF\x12\x34\x56", 12);

    QImage gray(3, 1, QImage::Format_Grayscale8);
    gray.scanLine(0)[0] = 0x10;
    gray.scanLine(0)[1] = 0x80;
    gray.scanLine(0)[2] = 0xF0;
    QTest::newRow("gray") << gray << QByteArray("DeviceGray") << QByteArray("\x10\x80\xF0", 3);
}

void ContentHelpersTest::imageXObjectLossless()
{
    QFETCH(QImage, image);
    QFETCH(QByteArray, colorSpace);
    QFETCH(QByteArray, samples);

    const PDFImagePageFactory::Frame frame = createFrame(image);
    QVERIFY(!frame.image.isNull());

    PDFDocumentBuilder builder;
    builder.createDocument();
    const ContentHelpersImage created = createImage(&builder, frame);
    QVERIFY(created.dictionary);

    // One object: the image without a mask and without a profile
    QCOMPARE(created.objectCountAfter, created.objectCountBefore + 1);

    // ISO 32000-2, Table 87
    if (created.dictionary->hasKey("Type"))
    {
        QCOMPARE(getName(builder, created.dictionary, "Type"), QByteArray("XObject"));
    }
    QCOMPARE(getName(builder, created.dictionary, "Subtype"), QByteArray("Image"));
    QCOMPARE(getInteger(builder, created.dictionary, "Width"), PDFInteger(image.width()));
    QCOMPARE(getInteger(builder, created.dictionary, "Height"), PDFInteger(image.height()));
    QCOMPARE(getInteger(builder, created.dictionary, "BitsPerComponent"), PDFInteger(8));
    QCOMPARE(getName(builder, created.dictionary, "ColorSpace"), colorSpace);
    QCOMPARE(getInteger(builder, created.dictionary, "Length"), PDFInteger(created.stream->getContent()->size()));
    QVERIFY(!created.dictionary->hasKey("SMask"));
    QVERIFY(!created.dictionary->hasKey("Mask"));
    QVERIFY(!created.dictionary->hasKey("Decode"));

    // Lossless: the reader gets the samples of the image, row by row from the top (8.9.5)
    QCOMPARE(builder.getDecodedStream(created.stream), samples);
}

void ContentHelpersTest::imageXObjectSoftMask()
{
    QImage image(2, 2, QImage::Format_ARGB32);
    image.setPixelColor(0, 0, QColor(255, 0, 0, 255));
    image.setPixelColor(1, 0, QColor(0, 255, 0, 128));
    image.setPixelColor(0, 1, QColor(0, 0, 255, 64));
    image.setPixelColor(1, 1, QColor(255, 255, 255, 0));

    PDFDocumentBuilder builder;
    builder.createDocument();
    const ContentHelpersImage created = createImage(&builder, createFrame(image));
    QVERIFY(created.dictionary);

    // Two objects: the soft mask, then the image
    QCOMPARE(created.objectCountAfter, created.objectCountBefore + 2);
    const PDFObject& maskReference = created.dictionary->get("SMask");
    QVERIFY(maskReference.isReference());
    QCOMPARE(size_t(maskReference.getReference().objectNumber), created.objectCountBefore);
    QCOMPARE(size_t(created.reference.objectNumber), created.objectCountBefore + 1);

    QCOMPARE(getName(builder, created.dictionary, "Subtype"), QByteArray("Image"));
    QCOMPARE(getInteger(builder, created.dictionary, "Width"), PDFInteger(2));
    QCOMPARE(getInteger(builder, created.dictionary, "Height"), PDFInteger(2));
    QCOMPARE(getInteger(builder, created.dictionary, "BitsPerComponent"), PDFInteger(8));
    QCOMPARE(getName(builder, created.dictionary, "ColorSpace"), QByteArray("DeviceRGB"));
    QVERIFY(!created.dictionary->hasKey("Mask"));

    // The colors are not blended with a matte color (the mask has no /Matte, 11.6.5.2): the
    // pixels, which are not transparent, keep their colors
    const QByteArray samples = builder.getDecodedStream(created.stream);
    QCOMPARE(samples.size(), qsizetype(12));
    QCOMPARE(samples.left(9), QByteArray("\xFF\x00\x00\x00\xFF\x00\x00\x00\xFF", 9));

    // ISO 32000-2, Table 143: the restrictions of a soft-mask image
    const PDFObject& maskObject = builder.getObjectByReference(maskReference.getReference());
    QVERIFY(maskObject.isStream());
    const PDFDictionary* mask = maskObject.getStream()->getDictionary();
    if (mask->hasKey("Type"))
    {
        QCOMPARE(getName(builder, mask, "Type"), QByteArray("XObject"));
    }
    QCOMPARE(getName(builder, mask, "Subtype"), QByteArray("Image"));
    QCOMPARE(getName(builder, mask, "ColorSpace"), QByteArray("DeviceGray"));
    QCOMPARE(getInteger(builder, mask, "Width"), PDFInteger(2));
    QCOMPARE(getInteger(builder, mask, "Height"), PDFInteger(2));
    QCOMPARE(getInteger(builder, mask, "BitsPerComponent"), PDFInteger(8));
    QVERIFY(!mask->hasKey("SMask"));
    QVERIFY(!mask->hasKey("Mask"));
    QVERIFY(!mask->hasKey("ImageMask"));
    QVERIFY(!mask->hasKey("Matte"));
    QVERIFY(!mask->hasKey("Decode"));

    // The samples of the mask are the opacity of the pixels (11.6.5.3)
    QCOMPARE(builder.getDecodedStream(maskObject.getStream()), QByteArray("\xFF\x80\x40\x00", 4));
}

void ContentHelpersTest::imageXObjectOpaqueAlpha()
{
    // An alpha channel, which is opaque everywhere, needs no soft mask
    QImage image(2, 1, QImage::Format_ARGB32);
    image.setPixelColor(0, 0, QColor(255, 0, 0, 255));
    image.setPixelColor(1, 0, QColor(0, 0, 255, 255));

    PDFDocumentBuilder builder;
    builder.createDocument();
    const ContentHelpersImage created = createImage(&builder, createFrame(image));
    QVERIFY(created.dictionary);

    QCOMPARE(created.objectCountAfter, created.objectCountBefore + 1);
    QVERIFY(!created.dictionary->hasKey("SMask"));
    QCOMPARE(getName(builder, created.dictionary, "ColorSpace"), QByteArray("DeviceRGB"));
    QCOMPARE(builder.getDecodedStream(created.stream), QByteArray("\xFF\x00\x00\x00\x00\xFF", 6));
}

void ContentHelpersTest::imageXObjectCompactedFrame()
{
    // A frame coded before its page is made (PDFImagePageFactory::compact) gives the same objects
    QImage image(2, 1, QImage::Format_ARGB32);
    image.setPixelColor(0, 0, QColor(255, 0, 0, 255));
    image.setPixelColor(1, 0, QColor(0, 255, 0, 32));

    const PDFImagePageFactory::Frame frame = createFrame(image);
    PDFImagePageFactory::Frame compactedFrame = frame;
    PDFImagePageFactory::compact(&compactedFrame);
    QVERIFY(compactedFrame.image.isNull());
    QVERIFY(!frame.image.isNull());

    PDFDocumentBuilder builder;
    builder.createDocument();
    const ContentHelpersImage created = createImage(&builder, frame);

    PDFDocumentBuilder compactedBuilder;
    compactedBuilder.createDocument();
    const ContentHelpersImage compacted = createImage(&compactedBuilder, compactedFrame);

    QVERIFY(created.dictionary);
    QVERIFY(compacted.dictionary);
    QCOMPARE(compacted.reference, created.reference);
    QCOMPARE(compacted.objectCountAfter, created.objectCountAfter);
    QVERIFY(compactedBuilder.getObjectByReference(compacted.reference) == builder.getObjectByReference(created.reference));

    const PDFObject& maskReference = created.dictionary->get("SMask");
    QVERIFY(maskReference.isReference());
    QVERIFY(compactedBuilder.getObjectByReference(maskReference.getReference()) == builder.getObjectByReference(maskReference.getReference()));
    QCOMPARE(compactedBuilder.getDecodedStream(compactedBuilder.getObjectByReference(maskReference.getReference()).getStream()), QByteArray("\xFF\x20", 2));
}

void ContentHelpersTest::imageXObjectIccProfile()
{
    QImage image(2, 1, QImage::Format_RGB888);
    image.setPixelColor(0, 0, QColor(255, 0, 0));
    image.setPixelColor(1, 0, QColor(0, 0, 255));
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));

    const PDFImagePageFactory::Frame frame = createFrame(image);
    QVERIFY(!frame.iccProfile.isEmpty());

    PDFDocumentBuilder builder;
    builder.createDocument();
    const ContentHelpersImage created = createImage(&builder, frame);
    QVERIFY(created.dictionary);

    // Two objects: the profile, then the image
    QCOMPARE(created.objectCountAfter, created.objectCountBefore + 2);
    QCOMPARE(size_t(created.reference.objectNumber), created.objectCountBefore + 1);

    // ISO 32000-2, 8.6.5.5: [/ICCBased stream]
    const PDFObject& colorSpace = builder.getObject(created.dictionary->get("ColorSpace"));
    QVERIFY(colorSpace.isArray());
    QCOMPARE(colorSpace.getArray()->getCount(), size_t(2));
    QCOMPARE(colorSpace.getArray()->getItem(0), PDFObject::createName("ICCBased"));
    QVERIFY(colorSpace.getArray()->getItem(1).isReference());
    QCOMPARE(size_t(colorSpace.getArray()->getItem(1).getReference().objectNumber), created.objectCountBefore);

    // Table 65: /N is the number of the components, /Alternate a color space of them
    const PDFObject& profile = builder.getObject(colorSpace.getArray()->getItem(1));
    QVERIFY(profile.isStream());
    QCOMPARE(getInteger(builder, profile.getStream()->getDictionary(), "N"), PDFInteger(3));
    QCOMPARE(getName(builder, profile.getStream()->getDictionary(), "Alternate"), QByteArray("DeviceRGB"));
    QCOMPARE(getInteger(builder, profile.getStream()->getDictionary(), "Length"), PDFInteger(frame.iccProfile.size()));
    QCOMPARE(*profile.getStream()->getContent(), frame.iccProfile);

    QCOMPARE(builder.getDecodedStream(created.stream), QByteArray("\xFF\x00\x00\x00\x00\xFF", 6));
}

void ContentHelpersTest::imageXObjectIccProfileOfOtherComponents()
{
    // A profile of three components cannot describe the samples of one component (Table 65:
    // /N shall match the number of the components of the profile): the device color space
    QImage image(3, 1, QImage::Format_Grayscale8);
    image.scanLine(0)[0] = 0x10;
    image.scanLine(0)[1] = 0x80;
    image.scanLine(0)[2] = 0xF0;

    PDFImagePageFactory::Frame frame = createFrame(image);
    frame.iccProfile = QColorSpace(QColorSpace::SRgb).iccProfile();
    QVERIFY(!frame.iccProfile.isEmpty());

    PDFDocumentBuilder builder;
    builder.createDocument();
    const ContentHelpersImage created = createImage(&builder, frame);
    QVERIFY(created.dictionary);

    QCOMPARE(created.objectCountAfter, created.objectCountBefore + 1);
    QCOMPARE(getName(builder, created.dictionary, "ColorSpace"), QByteArray("DeviceGray"));

    // The same for a JPEG of four components
    PDFImagePageFactory::Frame jpegFrame = createJpegFrame(4, false);
    jpegFrame.iccProfile = frame.iccProfile;
    const ContentHelpersImage jpeg = createImage(&builder, jpegFrame);
    QVERIFY(jpeg.dictionary);
    QCOMPARE(jpeg.objectCountAfter, jpeg.objectCountBefore + 1);
    QCOMPARE(getName(builder, jpeg.dictionary, "ColorSpace"), QByteArray("DeviceCMYK"));

    // A JPEG of three components takes the profile
    PDFImagePageFactory::Frame rgbFrame = createJpegFrame(3, false);
    rgbFrame.iccProfile = frame.iccProfile;
    const ContentHelpersImage rgb = createImage(&builder, rgbFrame);
    QVERIFY(rgb.dictionary);
    QCOMPARE(rgb.objectCountAfter, rgb.objectCountBefore + 2);
    const PDFObject& colorSpace = builder.getObject(rgb.dictionary->get("ColorSpace"));
    QVERIFY(colorSpace.isArray());
    QCOMPARE(colorSpace.getArray()->getItem(0), PDFObject::createName("ICCBased"));
}

void ContentHelpersTest::imageXObjectEmptyFrame()
{
    PDFDocumentBuilder builder;
    builder.createDocument();
    const size_t objectCount = builder.getStorage()->getObjects().size();

    // No pixels, no coded image, no JPEG
    const PDFImagePageFactory::Frame frame;
    QString message;
    try
    {
        PDFImagePageFactory::createImageXObject(&builder, frame);
    }
    catch (const PDFException& exception)
    {
        message = exception.getMessage();
    }
    QCOMPARE(message, QStringLiteral("The image is empty."));

    // Nothing was added to the document
    QCOMPARE(builder.getStorage()->getObjects().size(), objectCount);
}

void ContentHelpersTest::imagePageUsesImageXObject()
{
    // The page of an image holds the image XObject of createImageXObject, and the objects are
    // added in the same order: the profile, the soft mask, the image, the content, the page
    QImage image(2, 1, QImage::Format_ARGB32);
    image.setPixelColor(0, 0, QColor(255, 0, 0, 255));
    image.setPixelColor(1, 0, QColor(0, 255, 0, 32));
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const PDFImagePageFactory::Frame frame = createFrame(image);
    QVERIFY(!frame.iccProfile.isEmpty());

    PDFDocumentBuilder imageBuilder;
    imageBuilder.createDocument();
    const ContentHelpersImage created = createImage(&imageBuilder, frame);
    QVERIFY(created.dictionary);
    QCOMPARE(created.objectCountAfter, created.objectCountBefore + 3);

    PDFDocumentBuilder pageBuilder;
    pageBuilder.createDocument();
    const size_t first = pageBuilder.getStorage()->getObjects().size();
    QCOMPARE(first, created.objectCountBefore);
    const PDFObjectReference pageReference = PDFImagePageFactory::createPage(&pageBuilder, frame, PDFImagePageLayout());
    QCOMPARE(pageBuilder.getStorage()->getObjects().size(), first + 5);
    QCOMPARE(size_t(pageReference.objectNumber), first + 4);

    const PDFDictionary* page = pageBuilder.getDictionaryFromObject(pageBuilder.getObjectByReference(pageReference));
    QVERIFY(page);
    QCOMPARE(page->get("Contents"), PDFObject::createReference(PDFObjectReference(PDFInteger(first + 3), 0)));
    const PDFDictionary* resources = pageBuilder.getDictionaryFromObject(page->get("Resources"));
    QVERIFY(resources);
    const PDFDictionary* xobjects = pageBuilder.getDictionaryFromObject(resources->get("XObject"));
    QVERIFY(xobjects);
    QCOMPARE(xobjects->get("Im0"), PDFObject::createReference(PDFObjectReference(PDFInteger(first + 2), 0)));

    // The image, its soft mask and its profile are the objects of createImageXObject
    for (size_t i = 0; i < 3; ++i)
    {
        const PDFObjectReference reference(PDFInteger(first + i), 0);
        QVERIFY2(pageBuilder.getObjectByReference(reference) == imageBuilder.getObjectByReference(reference), qPrintable(QString::number(i)));
    }
    QCOMPARE(created.reference, PDFObjectReference(PDFInteger(first + 2), 0));
    QCOMPARE(created.dictionary->get("SMask"), PDFObject::createReference(PDFObjectReference(PDFInteger(first + 1), 0)));
}

void ContentHelpersTest::imagePageChecksEmptyFrameFirst()
{
    // An empty frame is reported before the margins, which leave no room
    PDFImagePageLayout layout;
    layout.pageSize = QSizeF(100.0, 100.0);
    layout.margins = QMarginsF(50.0, 0.0, 50.0, 0.0);

    PDFDocumentBuilder builder;
    builder.createDocument();

    QString emptyMessage;
    try
    {
        PDFImagePageFactory::createPage(&builder, PDFImagePageFactory::Frame(), layout);
    }
    catch (const PDFException& exception)
    {
        emptyMessage = exception.getMessage();
    }
    QCOMPARE(emptyMessage, QStringLiteral("The image is empty."));

    QImage image(2, 1, QImage::Format_RGB888);
    image.fill(Qt::red);
    QString marginsMessage;
    try
    {
        PDFImagePageFactory::createPage(&builder, createFrame(image), layout);
    }
    catch (const PDFException& exception)
    {
        marginsMessage = exception.getMessage();
    }
    QCOMPARE(marginsMessage, QStringLiteral("The margins leave no room for the image."));
}

// ------------------------------------------------------- Parameters of the functions

void ContentHelpersTest::exponentialFunctionParameters()
{
    // The values are binary fractions: they are compared exactly
    const PDFFunctionPtr function = createFunction("<< /FunctionType 2 /Domain [0.25 0.75] /Range [0 1 0 2 -1 1] /C0 [0.125 0.25 0.375] /C1 [0.875 0.75 0.625] /N 2.5 >>");
    QVERIFY(function);

    const PDFExponentialFunction* exponential = dynamic_cast<const PDFExponentialFunction*>(function.get());
    QVERIFY(exponential);

    // ISO 32000-2, Table 38
    QCOMPARE(exponential->getInputVariableCount(), uint32_t(1));
    QCOMPARE(exponential->getOutputVariableCount(), uint32_t(3));
    QCOMPARE(exponential->getDomain(), RealVector({ 0.25, 0.75 }));
    QCOMPARE(exponential->getRange(), RealVector({ 0.0, 1.0, 0.0, 2.0, -1.0, 1.0 }));

    // Table 40
    QCOMPARE(exponential->getC0(), RealVector({ 0.125, 0.25, 0.375 }));
    QCOMPARE(exponential->getC1(), RealVector({ 0.875, 0.75, 0.625 }));
    QCOMPARE(exponential->getExponent(), 2.5);
}

void ContentHelpersTest::exponentialFunctionDefaults()
{
    // ISO 32000-2, Table 40: /C0 is [0.0] and /C1 is [1.0], when they are missing; Table 38:
    // /Range is optional for a function of the type 2
    const PDFFunctionPtr function = createFunction("<< /FunctionType 2 /Domain [0 1] /N 1 >>");
    QVERIFY(function);

    const PDFExponentialFunction* exponential = dynamic_cast<const PDFExponentialFunction*>(function.get());
    QVERIFY(exponential);
    QCOMPARE(exponential->getOutputVariableCount(), uint32_t(1));
    QCOMPARE(exponential->getDomain(), RealVector({ 0.0, 1.0 }));
    QCOMPARE(exponential->getRange(), RealVector());
    QCOMPARE(exponential->getC0(), RealVector({ 0.0 }));
    QCOMPARE(exponential->getC1(), RealVector({ 1.0 }));
    QCOMPARE(exponential->getExponent(), 1.0);

    // Only one of them is missing
    const PDFFunctionPtr withC0 = createFunction("<< /FunctionType 2 /Domain [0 1] /C0 [0.5] /N 3 >>");
    const PDFExponentialFunction* exponentialWithC0 = dynamic_cast<const PDFExponentialFunction*>(withC0.get());
    QVERIFY(exponentialWithC0);
    QCOMPARE(exponentialWithC0->getC0(), RealVector({ 0.5 }));
    QCOMPARE(exponentialWithC0->getC1(), RealVector({ 1.0 }));
    QCOMPARE(exponentialWithC0->getExponent(), 3.0);

    const PDFFunctionPtr withC1 = createFunction("<< /FunctionType 2 /Domain [0 1] /C1 [0.25] /N 0.5 >>");
    const PDFExponentialFunction* exponentialWithC1 = dynamic_cast<const PDFExponentialFunction*>(withC1.get());
    QVERIFY(exponentialWithC1);
    QCOMPARE(exponentialWithC1->getC0(), RealVector({ 0.0 }));
    QCOMPARE(exponentialWithC1->getC1(), RealVector({ 0.25 }));
    QCOMPARE(exponentialWithC1->getExponent(), 0.5);
}

void ContentHelpersTest::stitchingFunctionParameters()
{
    // Three functions of the type 2 over the domain [-1 3] (ISO 32000-2, 7.10.4)
    const PDFFunctionPtr function = createFunction("<< /FunctionType 3 /Domain [-1 3] /Range [0 1] "
                                                   "/Functions [ << /FunctionType 2 /Domain [0 1] /C0 [0.125] /C1 [0.25] /N 1 >> "
                                                   "<< /FunctionType 2 /Domain [0 1] /C0 [0.375] /C1 [0.5] /N 2 >> "
                                                   "<< /FunctionType 2 /Domain [0 1] /C0 [0.625] /C1 [0.75] /N 3 >> ] "
                                                   "/Bounds [0.5 2] /Encode [0 1 1 0 0.25 0.75] >>");
    QVERIFY(function);

    const PDFStitchingFunction* stitching = dynamic_cast<const PDFStitchingFunction*>(function.get());
    QVERIFY(stitching);

    // Table 38
    QCOMPARE(stitching->getInputVariableCount(), uint32_t(1));
    QCOMPARE(stitching->getOutputVariableCount(), uint32_t(1));
    QCOMPARE(stitching->getDomain(), RealVector({ -1.0, 3.0 }));
    QCOMPARE(stitching->getRange(), RealVector({ 0.0, 1.0 }));

    // Table 41: k - 1 values of /Bounds, 2 x k values of /Encode
    QCOMPARE(stitching->getBounds(), RealVector({ 0.5, 2.0 }));
    QCOMPARE(stitching->getEncode(), RealVector({ 0.0, 1.0, 1.0, 0.0, 0.25, 0.75 }));

    // The k functions in their order, each with its interval of the domain: Domain0, the
    // values of /Bounds, Domain1
    const std::vector<PDFStitchingFunction::PartialFunction>& partialFunctions = stitching->getPartialFunctions();
    QCOMPARE(partialFunctions.size(), size_t(3));

    const RealVector lowerBounds = { -1.0, 0.5, 2.0 };
    const RealVector upperBounds = { 0.5, 2.0, 3.0 };
    const RealVector lowerEncode = { 0.0, 1.0, 0.25 };
    const RealVector upperEncode = { 1.0, 0.0, 0.75 };
    const RealVector c0 = { 0.125, 0.375, 0.625 };
    const RealVector c1 = { 0.25, 0.5, 0.75 };
    const RealVector exponents = { 1.0, 2.0, 3.0 };
    for (size_t i = 0; i < partialFunctions.size(); ++i)
    {
        QCOMPARE(partialFunctions[i].bound0, lowerBounds[i]);
        QCOMPARE(partialFunctions[i].bound1, upperBounds[i]);
        QCOMPARE(partialFunctions[i].encode0, lowerEncode[i]);
        QCOMPARE(partialFunctions[i].encode1, upperEncode[i]);

        const PDFExponentialFunction* exponential = dynamic_cast<const PDFExponentialFunction*>(partialFunctions[i].function.get());
        QVERIFY(exponential);
        QCOMPARE(exponential->getC0(), RealVector({ c0[i] }));
        QCOMPARE(exponential->getC1(), RealVector({ c1[i] }));
        QCOMPARE(exponential->getExponent(), exponents[i]);
        QCOMPARE(exponential->getDomain(), RealVector({ 0.0, 1.0 }));
    }
}

void ContentHelpersTest::stitchingFunctionOfOneFunction()
{
    // k = 1: /Bounds is empty, the function covers the whole domain (ISO 32000-2, 7.10.4, EXAMPLE 1)
    const PDFFunctionPtr function = createFunction("<< /FunctionType 3 /Domain [0.25 0.75] "
                                                   "/Functions [ << /FunctionType 2 /Domain [0 1] /C0 [0 0 0] /C1 [1 0.5 0.25] /N 1 >> ] "
                                                   "/Bounds [] /Encode [1 0] >>");
    QVERIFY(function);

    const PDFStitchingFunction* stitching = dynamic_cast<const PDFStitchingFunction*>(function.get());
    QVERIFY(stitching);
    QCOMPARE(stitching->getOutputVariableCount(), uint32_t(3));
    QCOMPARE(stitching->getDomain(), RealVector({ 0.25, 0.75 }));
    QCOMPARE(stitching->getRange(), RealVector());
    QCOMPARE(stitching->getBounds(), RealVector());
    QCOMPARE(stitching->getEncode(), RealVector({ 1.0, 0.0 }));

    QCOMPARE(stitching->getPartialFunctions().size(), size_t(1));
    QCOMPARE(stitching->getPartialFunctions()[0].bound0, 0.25);
    QCOMPARE(stitching->getPartialFunctions()[0].bound1, 0.75);
    QCOMPARE(stitching->getPartialFunctions()[0].encode0, 1.0);
    QCOMPARE(stitching->getPartialFunctions()[0].encode1, 0.0);
}

// ------------------------------------------------------- /ToUnicode of more code units

void ContentHelpersTest::toUnicodeText_data()
{
    QTest::addColumn<unsigned int>("code");
    QTest::addColumn<unsigned int>("byteCount");
    QTest::addColumn<QString>("expected");

    // beginbfchar: a destination is a string of UTF-16BE (ISO 32000-2, 9.10.3)
    QTest::newRow("bfchar ligature") << 0x0001u << 2u << QStringLiteral("ffi");
    QTest::newRow("bfchar surrogate pair") << 0x0002u << 2u << fromCodePoint(0x1F600);
    QTest::newRow("bfchar one code unit") << 0x0003u << 2u << QStringLiteral("A");
    QTest::newRow("bfchar odd number of bytes") << 0x0004u << 2u << QString();
    QTest::newRow("unmapped") << 0x0005u << 2u << QString();
    QTest::newRow("any length of the code") << 0x0001u << 0u << QStringLiteral("ffi");
    QTest::newRow("another length of the code") << 0x0001u << 1u << QString();

    // beginbfrange with one destination: the last byte is incremented for each code
    QTest::newRow("bfrange string, first code") << 0x0010u << 2u << QStringLiteral("fa");
    QTest::newRow("bfrange string, second code") << 0x0011u << 2u << QStringLiteral("fb");
    QTest::newRow("bfrange string, last code") << 0x0012u << 2u << QStringLiteral("fc");
    QTest::newRow("bfrange string, code after the range") << 0x0013u << 2u << QString();
    QTest::newRow("bfrange surrogate pair, first code") << 0x0050u << 2u << fromCodePoint(0x1F600);
    QTest::newRow("bfrange surrogate pair, last code") << 0x0052u << 2u << fromCodePoint(0x1F602);
    QTest::newRow("bfrange one code unit, first code") << 0x0030u << 2u << QStringLiteral("A");
    QTest::newRow("bfrange one code unit, last code") << 0x0032u << 2u << QStringLiteral("C");

    // beginbfrange with an array: a string for each code
    QTest::newRow("bfrange array, first code") << 0x0020u << 2u << QStringLiteral("ff");
    QTest::newRow("bfrange array, second code") << 0x0021u << 2u << QStringLiteral("fi");
    QTest::newRow("bfrange array, last code") << 0x0022u << 2u << QStringLiteral("ffl");
    QTest::newRow("bfrange array, code after the range") << 0x0023u << 2u << QString();
}

void ContentHelpersTest::toUnicodeText()
{
    QFETCH(unsigned int, code);
    QFETCH(unsigned int, byteCount);
    QFETCH(QString, expected);

    const PDFFontCMap cmap = PDFFontCMap::createFromData(getToUnicodeCMap());
    QVERIFY(cmap.isValid());

    QCOMPARE(cmap.getToUnicodeText(code, byteCount), expected);

    // The character of the code: the text of one code unit, no character for a text of more
    const QChar character = cmap.getToUnicode(code, byteCount);
    if (expected.size() == 1)
    {
        QCOMPARE(character, expected[0]);
    }
    else
    {
        // Also the later codes of a range with one destination of more code units: an entry of the
        // character 0 for the whole range gave them U+0001, U+0002 ... (fixed on 2026-11-25)
        QVERIFY2(character.isNull(), qPrintable(QString::number(character.unicode(), 16)));
    }
}

void ContentHelpersTest::toUnicodeTextRangePastLastByte_data()
{
    QTest::addColumn<unsigned int>("code");
    QTest::addColumn<QString>("expected");

    // ISO 32000-2, 9.10.3: the last byte of the destination shall not be incremented past 255.
    // The range <0040> <0044> <006600FE> breaks the rule: the codes up to the byte 255 are
    // mapped, the codes after it are not
    QTest::newRow("last byte 254") << 0x0040u << (QStringLiteral("f") + QChar(0x00FE));
    QTest::newRow("last byte 255") << 0x0041u << (QStringLiteral("f") + QChar(0x00FF));
    QTest::newRow("first code past 255") << 0x0042u << QString();
    QTest::newRow("last code past 255") << 0x0044u << QString();
}

void ContentHelpersTest::toUnicodeTextRangePastLastByte()
{
    QFETCH(unsigned int, code);
    QFETCH(QString, expected);

    const PDFFontCMap cmap = PDFFontCMap::createFromData("1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n"
                                                         "1 beginbfrange\n<0040> <0044> <006600FE>\nendbfrange\n");
    QVERIFY(cmap.isValid());

    const QString text = cmap.getToUnicodeText(code, 2);

    // Neither a carry into the high byte of the last code unit (U+0100) nor a wrapped byte
    QVERIFY2(expected.isEmpty() ? !text.startsWith(QLatin1Char('f')) : text == expected, qPrintable(text));

    // A code past the byte 255 has no text and no character either (the range has no entry of
    // the character 0, which gave U+0002 ... here)
    QCOMPARE(text, expected);
    if (expected.isEmpty())
    {
        QVERIFY(cmap.getToUnicode(code, 2).isNull());
    }
}

void ContentHelpersTest::toUnicodeTextCodeLength()
{
    // The code <41> of one byte and the code <0041> of two bytes are different codes (9.7.6.2)
    const PDFFontCMap cmap = PDFFontCMap::createFromData("2 begincodespacerange\n<00> <7F>\n<0000> <FFFF>\nendcodespacerange\n"
                                                         "2 beginbfchar\n<41> <0058>\n<0041> <006600660069>\nendbfchar\n");
    QVERIFY(cmap.isValid());

    QCOMPARE(cmap.getToUnicodeText(0x41, 1), QStringLiteral("X"));
    QCOMPARE(cmap.getToUnicodeText(0x41, 2), QStringLiteral("ffi"));
    QCOMPARE(cmap.getToUnicodeText(0x41, 3), QString());
    QCOMPARE(cmap.getToUnicode(0x41, 1), QChar(u'X'));
    QVERIFY(cmap.getToUnicode(0x41, 2).isNull());
}

void ContentHelpersTest::toUnicodeTextLongestDestination()
{
    // ISO 32000-2, 9.10.3: a destination may be a string of up to 512 bytes
    QVERIFY(PDFFontCMap::MAXIMUM_TO_UNICODE_TEXT_LENGTH * 2 >= 512);

    const qsizetype limit = PDFFontCMap::MAXIMUM_TO_UNICODE_TEXT_LENGTH;
    const QByteArray cmapData = "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n"
                                "3 beginbfchar\n"
                                "<0001> <" + QByteArray("0041").repeated(255) + "0042>\n"
                                "<0002> <" + QByteArray("0041").repeated(limit) + ">\n"
                                "<0003> <" + QByteArray("0041").repeated(limit + 1) + ">\n"
                                "endbfchar\n";
    const PDFFontCMap cmap = PDFFontCMap::createFromData(cmapData);
    QVERIFY(cmap.isValid());

    // 512 bytes of the standard
    QCOMPARE(cmap.getToUnicodeText(0x0001, 2), QString(255, QLatin1Char('A')) + QLatin1Char('B'));

    // The longest text of the library, a longer destination is not mapped
    QCOMPARE(cmap.getToUnicodeText(0x0002, 2), QString(limit, QLatin1Char('A')));
    QCOMPARE(cmap.getToUnicodeText(0x0003, 2), QString());
}

void ContentHelpersTest::toUnicodeTextCountLimit()
{
    // A map of more texts than MAXIMUM_TO_UNICODE_TEXT_COUNT (a hostile map): the texts over the
    // limit are not mapped
    const unsigned int limit = static_cast<unsigned int>(PDFFontCMap::MAXIMUM_TO_UNICODE_TEXT_COUNT);

    QByteArray cmapData = "1 begincodespacerange\n<000000> <FFFFFF>\nendcodespacerange\n";
    cmapData.reserve(24 * (limit + 2));
    cmapData += QByteArray::number(limit + 1) + " beginbfchar\n";
    for (unsigned int code = 0; code <= limit; ++code)
    {
        cmapData += "<" + QByteArray::number(code, 16).rightJustified(6, '0') + "> <00410042>\n";
    }
    cmapData += "endbfchar\n";

    const PDFFontCMap cmap = PDFFontCMap::createFromData(cmapData);
    QVERIFY(cmap.isValid());

    QCOMPARE(cmap.getToUnicodeText(0, 3), QStringLiteral("AB"));
    QCOMPARE(cmap.getToUnicodeText(limit - 1, 3), QStringLiteral("AB"));
    QCOMPARE(cmap.getToUnicodeText(limit, 3), QString());
}

void ContentHelpersTest::toUnicodeTextSerialization()
{
    // The texts are a part of the stored map
    const PDFFontCMap cmap = PDFFontCMap::deserialize(PDFFontCMap::createFromData(getToUnicodeCMap()).serialize());
    QVERIFY(cmap.isValid());

    QCOMPARE(cmap.getToUnicodeText(0x0001, 2), QStringLiteral("ffi"));
    QCOMPARE(cmap.getToUnicodeText(0x0002, 2), fromCodePoint(0x1F600));
    QCOMPARE(cmap.getToUnicodeText(0x0003, 2), QStringLiteral("A"));
    QCOMPARE(cmap.getToUnicodeText(0x0005, 2), QString());
    QCOMPARE(cmap.getToUnicodeText(0x0012, 2), QStringLiteral("fc"));
    QCOMPARE(cmap.getToUnicodeText(0x0022, 2), QStringLiteral("ffl"));
    QCOMPARE(cmap.getToUnicodeText(0x0032, 2), QStringLiteral("C"));
    QCOMPARE(cmap.getToUnicodeText(0x0001, 1), QString());
}

void ContentHelpersTest::toUnicodeTextSimpleFont_data()
{
    QTest::addColumn<QByteArray>("fontEntries");

    QTest::newRow("Type 1") << QByteArray("/Subtype /Type1 /BaseFont /Helvetica");
    QTest::newRow("TrueType") << QByteArray("/Subtype /TrueType /BaseFont /Arial");
}

void ContentHelpersTest::toUnicodeTextSimpleFont()
{
    QFETCH(QByteArray, fontEntries);

    // ISO 32000-2, 9.10.3: the codes of a simple font are written as one byte
    const ContentHelpersFont fixture = createFont(fontEntries,
                                                  "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
                                                  "3 beginbfchar\n<41> <006600660069>\n<42> <D83DDE00>\n<43> <0058>\nendbfchar\n"
                                                  "1 beginbfrange\n<50> <52> <00660061>\nendbfrange\n");
    const PDFFontPointer font = fixture.font;
    QVERIFY(font);
    QVERIFY(dynamic_cast<const PDFSimpleFont*>(font.get()));

    QCOMPARE(font->getToUnicodeText(0x41, 1), QStringLiteral("ffi"));
    QCOMPARE(font->getToUnicodeText(0x42, 1), fromCodePoint(0x1F600));
    QCOMPARE(font->getToUnicodeText(0x43, 1), QStringLiteral("X"));
    QCOMPARE(font->getToUnicodeText(0x50, 1), QStringLiteral("fa"));
    QCOMPARE(font->getToUnicodeText(0x51, 1), QStringLiteral("fb"));
    QCOMPARE(font->getToUnicodeText(0x52, 1), QStringLiteral("fc"));

    // The code D is in the encoding of the font, not in its /ToUnicode
    QCOMPARE(font->getToUnicodeText(0x44, 1), QString());

    // The map of a simple font is still not given out (Document Report reads it as "no mapping of its own")
    QVERIFY(font->getToUnicode() == nullptr);
}

void ContentHelpersTest::toUnicodeTextSimpleFontCodeLength()
{
    const ContentHelpersFont fixture = createFont("/Subtype /Type1 /BaseFont /Helvetica",
                                                  "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
                                                  "1 beginbfrange\n<50> <52> <00660061>\nendbfrange\n");
    const PDFFontPointer font = fixture.font;
    QVERIFY(font);

    // A code of a simple font is one byte (ISO 32000-2, 9.4.3): the length 1 and any length (0)
    // find it, a code of more bytes or over 255 is no code of the font
    QCOMPARE(font->getToUnicodeText(0x50, 1), QStringLiteral("fa"));
    QCOMPARE(font->getToUnicodeText(0x50, 0), QStringLiteral("fa"));
    QCOMPARE(font->getToUnicodeText(0x50, 2), QString());
    QCOMPARE(font->getToUnicodeText(0x150, 1), QString());
}

void ContentHelpersTest::toUnicodeTextType0Font()
{
    // The font and the ToUnicode CMap of ISO 32000-2, 9.10.3, EXAMPLE 1 and EXAMPLE 2
    const ContentHelpersFont fixture = createFont("/Subtype /Type0 /BaseFont /Ryumin-Light /Encoding /Identity-H "
                                                  "/DescendantFonts [ << /Type /Font /Subtype /CIDFontType2 /BaseFont /Ryumin-Light "
                                                  "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> >> ]",
                                                  getStandardToUnicodeCMap());
    const PDFFontPointer font = fixture.font;
    QVERIFY(font);
    QCOMPARE(font->getFontType(), FontType::Type0);

    // <0000> to <005E> are SPACE to TILDE
    QCOMPARE(font->getToUnicodeText(0x0000, 2), QStringLiteral(" "));
    QCOMPARE(font->getToUnicodeText(0x0021, 2), QStringLiteral("A"));
    QCOMPARE(font->getToUnicodeText(0x005E, 2), QStringLiteral("~"));

    // The ligatures ff, fi and ffl
    QCOMPARE(font->getToUnicodeText(0x005F, 2), QStringLiteral("ff"));
    QCOMPARE(font->getToUnicodeText(0x0060, 2), QStringLiteral("fi"));
    QCOMPARE(font->getToUnicodeText(0x0061, 2), QStringLiteral("ffl"));

    // U+2003E as <D840DC3E>
    QCOMPARE(font->getToUnicodeText(0x3A51, 2), fromCodePoint(0x2003E));
    QCOMPARE(font->getToUnicodeText(0x3A51, 2), QString(QChar(0xD840)) + QChar(0xDC3E));

    // A code, which is not mapped
    QCOMPARE(font->getToUnicodeText(0x0062, 2), QString());

    // The character of a code of more code units stays null
    const PDFFontCMap* toUnicode = font->getToUnicode();
    QVERIFY(toUnicode && toUnicode->isValid());
    QVERIFY(toUnicode->getToUnicode(0x005F, 2).isNull());
    QVERIFY(toUnicode->getToUnicode(0x3A51, 2).isNull());
    QCOMPARE(toUnicode->getToUnicode(0x0021, 2), QChar(u'A'));
}

void ContentHelpersTest::toUnicodeTextType0FontCodeLength()
{
    const ContentHelpersFont fixture = createFont("/Subtype /Type0 /BaseFont /Ryumin-Light /Encoding /Identity-H "
                                                  "/DescendantFonts [ << /Type /Font /Subtype /CIDFontType2 /BaseFont /Ryumin-Light "
                                                  "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> >> ]",
                                                  getStandardToUnicodeCMap());
    const PDFFontPointer font = fixture.font;
    QVERIFY(font);

    // A code of a composite font is matched with its length (the codes of the map have two
    // bytes); 0 is any length
    QCOMPARE(font->getToUnicodeText(0x0021, 2), QStringLiteral("A"));
    QCOMPARE(font->getToUnicodeText(0x0021, 1), QString());
    QCOMPARE(font->getToUnicodeText(0x0021, 3), QString());
    QCOMPARE(font->getToUnicodeText(0x0021, 0), QStringLiteral("A"));
    QCOMPARE(font->getToUnicodeText(0x005F, 1), QString());
    QCOMPARE(font->getToUnicodeText(0x005F, 0), QStringLiteral("ff"));
}

// ------------------------------------------------- widths of the font dictionaries

void ContentHelpersTest::fontWidthAccessors_data()
{
    // The accessor, its argument (a character code or a CID; unused for an accessor without one)
    // and the expected value (a truth value as 1 or 0), all written by hand from the dictionary
    QTest::addColumn<QByteArray>("fontEntries");
    QTest::addColumn<QByteArray>("accessor");
    QTest::addColumn<int>("argument");
    QTest::addColumn<double>("expected");

    // A simple font (ISO 32000-2, 9.6.2.1, Table 109): /Widths has the widths of the codes from
    // /FirstChar on. Here 65, 66 and 67 have a width - 66 the width 0, which is a width -, and 68
    // is inside of /FirstChar ... /LastChar, but /Widths ends before it: no width in the dictionary
    const QByteArray simple = "/Subtype /Type1 /BaseFont /Helvetica /FirstChar 65 /LastChar 68 /Widths [ 667 0 722 ]";
    QTest::newRow("simple-has-below-first-char") << simple << QByteArray("hasGlyphAdvance") << 64 << 0.0;
    QTest::newRow("simple-has-first-char") << simple << QByteArray("hasGlyphAdvance") << 65 << 1.0;
    QTest::newRow("simple-has-width-zero") << simple << QByteArray("hasGlyphAdvance") << 66 << 1.0;
    QTest::newRow("simple-has-last-width") << simple << QByteArray("hasGlyphAdvance") << 67 << 1.0;
    QTest::newRow("simple-has-past-widths") << simple << QByteArray("hasGlyphAdvance") << 68 << 0.0;
    QTest::newRow("simple-has-above-last-char") << simple << QByteArray("hasGlyphAdvance") << 69 << 0.0;
    QTest::newRow("simple-advance-first-char") << simple << QByteArray("getGlyphAdvance") << 65 << 667.0;
    QTest::newRow("simple-advance-width-zero") << simple << QByteArray("getGlyphAdvance") << 66 << 0.0;
    QTest::newRow("simple-advance-past-widths") << simple << QByteArray("getGlyphAdvance") << 68 << 0.0;
    QTest::newRow("simple-first-char") << simple << QByteArray("getFirstChar") << 0 << 65.0;
    QTest::newRow("simple-last-char") << simple << QByteArray("getLastChar") << 0 << 68.0;

    // A standard font without /Widths (allowed for the standard 14 fonts): no code has a width
    const QByteArray standard = "/Subtype /Type1 /BaseFont /Helvetica";
    QTest::newRow("standard-has-none") << standard << QByteArray("hasGlyphAdvance") << 65 << 0.0;

    // A Type 3 font (9.6.4, Table 110) with the widths of 65 (500) and 66 (0); 67 is /LastChar
    // without an item of /Widths
    const QByteArray type3 = "/Subtype /Type3 /FontBBox [ 0 0 1000 1000 ] /FontMatrix [ 0.001 0 0 0.001 0 0 ] /CharProcs << >> "
                             "/Encoding << /Type /Encoding /Differences [ ] >> /FirstChar 65 /LastChar 67 /Widths [ 500 0 ]";
    QTest::newRow("type3-has-below-first-char") << type3 << QByteArray("hasWidth") << 64 << 0.0;
    QTest::newRow("type3-has-first-char") << type3 << QByteArray("hasWidth") << 65 << 1.0;
    QTest::newRow("type3-has-width-zero") << type3 << QByteArray("hasWidth") << 66 << 1.0;
    QTest::newRow("type3-has-past-widths") << type3 << QByteArray("hasWidth") << 67 << 0.0;
    QTest::newRow("type3-has-above-last-char") << type3 << QByteArray("hasWidth") << 68 << 0.0;
    QTest::newRow("type3-width-first-char") << type3 << QByteArray("getWidth") << 65 << 500.0;
    QTest::newRow("type3-width-past-widths") << type3 << QByteArray("getWidth") << 67 << 0.0;

    // A Type 0 font (9.7.4.3): /W [ 10 [ 500 0 ] 20 22 600 ] gives the CIDs 10 (500), 11 (0) and
    // 20 to 22 (600) a width of their own, every other CID has the default width /DW 750
    const QByteArray type0 = "/Subtype /Type0 /BaseFont /Ryumin-Light /Encoding /Identity-H "
                             "/DescendantFonts [ << /Type /Font /Subtype /CIDFontType2 /BaseFont /Ryumin-Light "
                             "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
                             "/DW 750 /W [ 10 [ 500 0 ] 20 22 600 ] >> ]";
    QTest::newRow("type0-explicit-before-array") << type0 << QByteArray("hasExplicitGlyphAdvance") << 9 << 0.0;
    QTest::newRow("type0-explicit-array-first") << type0 << QByteArray("hasExplicitGlyphAdvance") << 10 << 1.0;
    QTest::newRow("type0-explicit-width-zero") << type0 << QByteArray("hasExplicitGlyphAdvance") << 11 << 1.0;
    QTest::newRow("type0-explicit-after-array") << type0 << QByteArray("hasExplicitGlyphAdvance") << 12 << 0.0;
    QTest::newRow("type0-explicit-before-range") << type0 << QByteArray("hasExplicitGlyphAdvance") << 19 << 0.0;
    QTest::newRow("type0-explicit-range-first") << type0 << QByteArray("hasExplicitGlyphAdvance") << 20 << 1.0;
    QTest::newRow("type0-explicit-range-last") << type0 << QByteArray("hasExplicitGlyphAdvance") << 22 << 1.0;
    QTest::newRow("type0-explicit-after-range") << type0 << QByteArray("hasExplicitGlyphAdvance") << 23 << 0.0;
    QTest::newRow("type0-default") << type0 << QByteArray("getDefaultGlyphAdvance") << 0 << 750.0;
    QTest::newRow("type0-advance-explicit") << type0 << QByteArray("getGlyphAdvance") << 21 << 600.0;
    QTest::newRow("type0-advance-width-zero") << type0 << QByteArray("getGlyphAdvance") << 11 << 0.0;
    QTest::newRow("type0-advance-default") << type0 << QByteArray("getGlyphAdvance") << 12 << 750.0;

    // Without /DW the default width is 1000 (9.7.4.1, Table 115)
    const QByteArray type0WithoutDefault = "/Subtype /Type0 /BaseFont /Ryumin-Light /Encoding /Identity-H "
                                           "/DescendantFonts [ << /Type /Font /Subtype /CIDFontType2 /BaseFont /Ryumin-Light "
                                           "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> >> ]";
    QTest::newRow("type0-default-missing") << type0WithoutDefault << QByteArray("getDefaultGlyphAdvance") << 0 << 1000.0;
    QTest::newRow("type0-explicit-none") << type0WithoutDefault << QByteArray("hasExplicitGlyphAdvance") << 10 << 0.0;
}

void ContentHelpersTest::fontWidthAccessors()
{
    QFETCH(QByteArray, fontEntries);
    QFETCH(QByteArray, accessor);
    QFETCH(int, argument);
    QFETCH(double, expected);

    const ContentHelpersFont fixture = createFont(fontEntries, "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n");
    const PDFFontPointer font = fixture.font;
    QVERIFY(font);

    std::optional<double> actual;
    if (const PDFSimpleFont* simpleFont = dynamic_cast<const PDFSimpleFont*>(font.data()))
    {
        if (accessor == "hasGlyphAdvance")
        {
            actual = simpleFont->hasGlyphAdvance(size_t(argument)) ? 1.0 : 0.0;
        }
        else if (accessor == "getGlyphAdvance")
        {
            actual = double(simpleFont->getGlyphAdvance(size_t(argument)));
        }
        else if (accessor == "getFirstChar")
        {
            actual = double(simpleFont->getFirstChar());
        }
        else if (accessor == "getLastChar")
        {
            actual = double(simpleFont->getLastChar());
        }
    }
    else if (const PDFType3Font* type3Font = dynamic_cast<const PDFType3Font*>(font.data()))
    {
        if (accessor == "hasWidth")
        {
            actual = type3Font->hasWidth(argument) ? 1.0 : 0.0;
        }
        else if (accessor == "getWidth")
        {
            actual = type3Font->getWidth(argument);
        }
    }
    else if (const PDFType0Font* type0Font = dynamic_cast<const PDFType0Font*>(font.data()))
    {
        if (accessor == "hasExplicitGlyphAdvance")
        {
            actual = type0Font->hasExplicitGlyphAdvance(CID(argument)) ? 1.0 : 0.0;
        }
        else if (accessor == "getDefaultGlyphAdvance")
        {
            actual = type0Font->getDefaultGlyphAdvance();
        }
        else if (accessor == "getGlyphAdvance")
        {
            actual = type0Font->getGlyphAdvance(CID(argument));
        }
    }

    // The font is of the kind of its accessor
    QVERIFY2(actual.has_value(), accessor.constData());
    QCOMPARE(*actual, expected);
}

QTEST_MAIN(ContentHelpersTest)

#include "tst_contenthelperstest.moc"
