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

#ifndef XFATESTSAMPLES_H
#define XFATESTSAMPLES_H

#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"

#include <QBuffer>
#include <QByteArray>
#include <QSizeF>
#include <QString>

#include <vector>

namespace pdftest
{

/// Documents with an XFA form for the tests of the paging of XFA (step 8): the
/// pages of the PDF document and the template of the XFA form are chosen by the
/// test. The template is the packet "template" of the XFA array (as Adobe writes it).
class XFATestSamples
{
public:
    XFATestSamples() = delete;

    /// Letter (8.5 x 11 in) in points
    static QSizeF letter() { return QSizeF(612.0, 792.0); }

    /// A4 in points
    static QSizeF a4() { return QSizeF(595.0, 842.0); }

    /// Template, whose layout has \p pageCount pages of the medium \p shortSide x
    /// \p longSide (inches, portrait): one subform per page, each with a text and
    /// a break to the next page area (except the last one)
    static QByteArray pagedTemplate(int pageCount, const QString& shortSide = QStringLiteral("8.5in"), const QString& longSide = QStringLiteral("11in"))
    {
        QByteArray xml = "<template xmlns=\"http://www.xfa.org/schema/xfa-template/3.3/\">\n"
                         "<subform name=\"form1\" layout=\"tb\">\n";
        xml += "<pageSet><pageArea name=\"Page1\"><contentArea x=\"0.25in\" y=\"0.25in\" w=\"" + shortSide.toLatin1() + "\" h=\"" + longSide.toLatin1() +
               "\"/><medium stock=\"custom\" short=\"" + shortSide.toLatin1() + "\" long=\"" + longSide.toLatin1() + "\"/></pageArea></pageSet>\n";
        for (int i = 0; i < pageCount; ++i)
        {
            xml += "<subform name=\"page" + QByteArray::number(i + 1) + "\" layout=\"tb\" w=\"4in\" h=\"1in\">"
                   "<draw name=\"title\" w=\"3in\" h=\"0.5in\"><value><text>XFA page " + QByteArray::number(i + 1) + "</text></value></draw>";
            if (i + 1 < pageCount)
            {
                xml += "<breakAfter targetType=\"pageArea\"/>";
            }
            xml += "</subform>\n";
        }
        xml += "</subform>\n</template>\n";
        return xml;
    }

    /// Template of a hostile writer: \p occurrences repetitions of a subform, which
    /// breaks to the next page area each time (more pages than the limit of the
    /// layout engine)
    static QByteArray manyPagesTemplate(int occurrences)
    {
        QByteArray xml = "<template xmlns=\"http://www.xfa.org/schema/xfa-template/3.3/\">\n"
                         "<subform name=\"form1\" layout=\"tb\">\n"
                         "<pageSet><pageArea name=\"Page1\"><contentArea x=\"0.25in\" y=\"0.25in\" w=\"8in\" h=\"10.5in\"/>"
                         "<medium stock=\"letter\" short=\"8.5in\" long=\"11in\"/></pageArea></pageSet>\n";
        // The occurrences repeat the content of the subform: its inner subform with
        // the break after it
        xml += "<subform name=\"repeated\" layout=\"tb\"><occur initial=\"" + QByteArray::number(occurrences) + "\" min=\"" +
               QByteArray::number(occurrences) + "\" max=\"-1\"/>"
               "<subform name=\"page\" layout=\"tb\" w=\"4in\" h=\"1in\">"
               "<draw name=\"title\" w=\"3in\" h=\"0.5in\"><value><text>Repeated</text></value></draw>"
               "<breakAfter targetType=\"pageArea\"/></subform></subform>\n";
        xml += "</subform>\n</template>\n";
        return xml;
    }

    /// A template, which is not XML
    static QByteArray garbageTemplate() { return QByteArray("<template><subform name=\"form1\"<<<& garbage \x01\x02 </pageSet>"); }

    /// Document with the pages \p pageSizes and an XFA form with the template
    /// \p templateXml (an empty template = an AcroForm without XFA)
    static pdf::PDFDocument createDocument(const std::vector<QSizeF>& pageSizes, const QByteArray& templateXml)
    {
        pdf::PDFDocumentBuilder builder;
        for (const QSizeF& size : pageSizes)
        {
            builder.appendPage(QRectF(QPointF(0, 0), size));
        }

        const pdf::PDFObjectReference acroForm = builder.createAcroForm({});
        if (!templateXml.isEmpty())
        {
            pdf::PDFDictionaryBuilder streamDictionary;
            streamDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(templateXml.size()));
            const pdf::PDFObjectReference stream = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(streamDictionary), QByteArray(templateXml))));

            pdf::PDFArrayBuilder xfa;
            xfa.appendItem(pdf::PDFObject::createString(QByteArray("template")));
            xfa.appendItem(pdf::PDFObject::createReference(stream));

            pdf::PDFDictionaryBuilder form;
            form.setEntry(pdf::PDFInplaceOrMemoryString("XFA"), pdf::PDFObject::createArray(std::move(xfa)));
            builder.mergeTo(acroForm, pdf::PDFObject::createDictionary(std::move(form)));
        }
        builder.setCatalogAcroForm(acroForm);
        return builder.build();
    }

    /// The bytes of the document written by PDFDocumentWriter
    static QByteArray write(const pdf::PDFDocument& document)
    {
        QByteArray data;
        QBuffer buffer(&data);
        if (!buffer.open(QIODevice::WriteOnly))
        {
            return QByteArray();
        }
        pdf::PDFDocumentWriter writer(nullptr);
        if (!writer.write(&buffer, &document))
        {
            return QByteArray();
        }
        buffer.close();
        return data;
    }

    /// Media boxes of the pages of \p document
    static std::vector<QSizeF> getPageSizes(const pdf::PDFDocument* document)
    {
        std::vector<QSizeF> sizes;
        const pdf::PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
        for (size_t i = 0; catalog && i < catalog->getPageCount(); ++i)
        {
            sizes.push_back(catalog->getPage(i)->getMediaBox().size());
        }
        return sizes;
    }
};

}   // namespace pdftest

#endif // XFATESTSAMPLES_H
