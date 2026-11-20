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

#ifndef APPMCPREVIEWDOCUMENTS_H
#define APPMCPREVIEWDOCUMENTS_H

// The documents of the MCP tests of Compare and Measure (step 7c of phase 2, UnitTestsAppMcpReview), written by
// the test from objects as text:
//   - Compare: pages 600 x 800 with a Type 3 font (ISO 32000-2, 9.6.4) of three glyphs - "A" a square of its em,
//     "B" the left half of it, the space empty - with a /ToUnicode map (9.10.3), so the text of a page is known
//     exactly; the vector graphics are rectangles of known coordinates (the documents of tst_difftest.cpp, whose
//     counts of the differences are the expectations of the core);
//   - Measure: pages 600 x 800 (MergeTestPdf::createPages) with a viewport of a measure (12.9.2, Table 265 - 268)
//     or a certification (12.8.2.2, Table 257).

#include "mergetestpdf.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QFile>
#include <QString>

namespace pdftest
{

class AppMcpReviewDocuments
{
public:
    AppMcpReviewDocuments() = delete;

    /// A document of the pages of the contents (media box 600 x 800, the font /F1 of the glyphs A, B, space)
    static mergetest::MergeTestPdf createTextPdf(const QByteArrayList& pageContents)
    {
        mergetest::MergeTestPdf pdf;
        pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");

        const QByteArray squareGlyph = mergetest::MergeTestPdf::stream("1000 0 0 0 1000 1000 d1 0 0 1000 1000 re f");
        const QByteArray halfGlyph = mergetest::MergeTestPdf::stream("1000 0 0 0 500 1000 d1 0 0 500 1000 re f");
        const QByteArray spaceGlyph = mergetest::MergeTestPdf::stream("1000 0 d0");
        const QByteArray toUnicode = mergetest::MergeTestPdf::stream("/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def "
                                                                     "/CMapName /Adobe-Identity-UCS def /CMapType 2 def 1 begincodespacerange <00> <FF> endcodespacerange "
                                                                     "3 beginbfchar <20> <0020> <41> <0041> <42> <0042> endbfchar endcmap CMapName currentdict /CMap defineresource pop end end");
        QByteArray widths = "1000";
        for (int code = 33; code < 65; ++code)
        {
            widths += " 0";
        }
        widths += " 1000 1000";
        pdf.set(5, "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] /CharProcs << /A 6 0 R /B 7 0 R /space 8 0 R >> "
                   "/Encoding << /Type /Encoding /Differences [32 /space 65 /A /B] >> /FirstChar 32 /LastChar 66 /Widths [" + widths + "] /Resources << >> /ToUnicode 9 0 R >>");
        pdf.set(6, squareGlyph);
        pdf.set(7, halfGlyph);
        pdf.set(8, spaceGlyph);
        pdf.set(9, toUnicode);

        QByteArray kids;
        for (qsizetype i = 0; i < pageContents.size(); ++i)
        {
            const int pageObject = 10 + int(i) * 2;
            kids += QByteArray::number(pageObject) + " 0 R ";
            pdf.set(pageObject, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /Font << /F1 5 0 R >> >> /Contents " + QByteArray::number(pageObject + 1) + " 0 R >>");
            pdf.set(pageObject + 1, mergetest::MergeTestPdf::stream(pageContents[i]));
        }
        pdf.set(2, "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(pageContents.size()) + " >>");
        return pdf;
    }

    /// A text of the font /F1 of the size at the position
    static QByteArray text(const QByteArray& characters, int x, int y, int size = 10)
    {
        return "BT /F1 " + QByteArray::number(size) + " Tf " + QByteArray::number(x) + " " + QByteArray::number(y) + " Td (" + characters + ") Tj ET ";
    }

    /// A blue filled rectangle
    static QByteArray rect(double x, double y, double width, double height)
    {
        return "0 0 1 rg " + QByteArray::number(x, 'f', 3) + " " + QByteArray::number(y, 'f', 3) + " " + QByteArray::number(width, 'f', 3) + " " + QByteArray::number(height, 'f', 3) + " re f ";
    }

    /// The original of the pair of tst_difftest.cpp: page 1 the word "AB" and a rectangle at (100, 100), page 2
    /// the words "AAAA AB"
    static mergetest::MergeTestPdf createOriginal()
    {
        return createTextPdf({ text("AB", 50, 700) + rect(100, 100, 50, 20), text("AAAA AB", 50, 700) });
    }

    /// The revised document of the pair: page 1 the rectangle at (300, 500) instead, page 2 "AAAA" became "BBBB"
    static mergetest::MergeTestPdf createRevised()
    {
        return createTextPdf({ text("AB", 50, 700) + rect(300, 500, 40, 30), text("BBBB AB", 50, 700) });
    }

    /// One page with \p count small rectangles more than the page of createSquares(0) (each one an added vector
    /// graphic)
    static mergetest::MergeTestPdf createSquares(int count)
    {
        QByteArray content = rect(1, 1, 1, 1);
        for (int i = 0; i < count; ++i)
        {
            content += rect(10 + (i % 50) * 11, 10 + (i / 50) * 11, 5, 5);
        }
        return createTextPdf({ content });
    }

    /// The file of the document encrypted by AES 256 with an empty user password (it opens without a question),
    /// the owner password "owner" and the \p permissions (Table 22)
    static QByteArray encrypt(const mergetest::MergeTestPdf& pdf, uint32_t permissions)
    {
        return mergetest::MergeTestPdf::encrypt(pdf.read(), QString(), QStringLiteral("owner"), permissions);
    }

    /// The page of a viewport "Plan" over [0 0 300 300] with a measure 1:100 (12.9.2: /X converts a point of the
    /// default user space into 100 × 25.4 / 72 mm, Table 268 /C), the first of two pages of 600 x 800
    static mergetest::MergeTestPdf createMeasurePages()
    {
        const QByteArray conversion = QByteArray::number(100.0 * 25.4 / 72.0, 'g', 17);
        const QByteArray viewport = "/VP [<< /Type /Viewport /BBox [0 0 300 300] /Name (Plan) /Measure << /Type /Measure /Subtype /RL /R (1:100) "
                                    "/X [<< /Type /NumberFormat /U (mm) /C " + conversion + " >>] /D [<< /Type /NumberFormat /U (mm) /C 1 >>] "
                                    "/A [<< /Type /NumberFormat /U (sq. mm) /C 1 >>] >> >>]";
        return mergetest::MergeTestPdf::createPages("M", 2, QByteArray(), { { 1, viewport } });
    }

    /// A document of 2 pages certified with the level \p level (12.8.2.2: /Perms /DocMDP refers to the signature
    /// of the field "cert" with the transform method DocMDP and /P, Table 257)
    static mergetest::MergeTestPdf createCertified(int level)
    {
        mergetest::MergeTestPdf pdf = mergetest::MergeTestPdf::createPages("C", 2, "/AcroForm << /Fields [60 0 R] /SigFlags 3 >> /Perms << /DocMDP 61 0 R >>", { { 1, "/Annots [60 0 R]" } });
        pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (cert) /V 61 0 R /Rect [0 0 0 0] /P 3 0 R >>");
        pdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> /Reference [<< /TransformMethod /DocMDP /TransformParams << /P " +
                        QByteArray::number(level) + " >> >>] >>");
        return pdf;
    }

    /// Writes the bytes into the file; the path, or empty on an error
    static QString write(const QString& filePath, const QByteArray& data)
    {
        QFile file(filePath);
        return file.open(QFile::WriteOnly | QFile::Truncate) && file.write(data) == data.size() ? filePath : QString();
    }
};

}   // namespace pdftest

#endif // APPMCPREVIEWDOCUMENTS_H
