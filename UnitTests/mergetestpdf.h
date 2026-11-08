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

#ifndef MERGETESTPDF_H
#define MERGETESTPDF_H

// The documents of the tests of the merge (UnitTestsDocumentMerger, UnitTestsDocumentToolMerge,
// UnitTestsDocumentManipulator): PDF text of numbered objects written by the test, so every
// object, which points at a page, is known, and the readers of the result.

#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfencoding.h"
#include "pdfsecurityhandler.h"

#include <QBuffer>
#include <QDebug>

#include <map>

namespace mergetest
{

/// Writes PDF text of numbered objects (a classic cross-reference table)
class MergeTestPdf
{
public:
    /// Sets the object with its text (a dictionary, an array, a stream by stream())
    void set(int number, const QByteArray& text) { m_objects[number] = text; }

    /// Extra entries of the trailer (/Info, /ID)
    void setTrailerEntries(const QByteArray& entries) { m_trailerEntries = entries; }

    /// Version of the header
    void setVersion(const QByteArray& version) { m_version = version; }

    /// A stream object of the content with the extra entries of its dictionary
    static QByteArray stream(const QByteArray& content, const QByteArray& entries = QByteArray())
    {
        return "<< /Length " + QByteArray::number(content.size()) + " " + entries + " >>\nstream\n" + content + "\nendstream";
    }

    QByteArray write() const
    {
        QByteArray data = "%PDF-" + m_version + "\n";
        const int size = m_objects.empty() ? 1 : m_objects.rbegin()->first + 1;
        std::vector<qint64> offsets(size_t(size), -1);
        for (const auto& [number, text] : m_objects)
        {
            offsets[size_t(number)] = data.size();
            data += QByteArray::number(number) + " 0 obj\n" + text + "\nendobj\n";
        }
        const qint64 xref = data.size();
        data += "xref\n0 " + QByteArray::number(size) + "\n0000000000 65535 f \n";
        for (int i = 1; i < size; ++i)
        {
            data += offsets[size_t(i)] < 0 ? QByteArray("0000000000 65535 f \n") : QByteArray::number(offsets[size_t(i)]).rightJustified(10, '0') + " 00000 n \n";
        }
        data += "trailer\n<< /Size " + QByteArray::number(size) + " /Root 1 0 R " + m_trailerEntries + " >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return data;
    }

    /// The same objects with a cross-reference stream instead of the table: its dictionary is the
    /// trailer (ISO 32000-2, 7.5.8; /W [1 4 2], no filter)
    QByteArray writeWithCrossReferenceStream() const
    {
        QByteArray data = "%PDF-" + m_version + "\n";
        const int size = (m_objects.empty() ? 1 : m_objects.rbegin()->first + 1) + 1;
        std::vector<qint64> offsets(size_t(size), -1);
        for (const auto& [number, text] : m_objects)
        {
            offsets[size_t(number)] = data.size();
            data += QByteArray::number(number) + " 0 obj\n" + text + "\nendobj\n";
        }

        const int xrefNumber = size - 1;
        offsets[size_t(xrefNumber)] = data.size();
        QByteArray rows;
        for (int i = 0; i < size; ++i)
        {
            const bool isUsed = offsets[size_t(i)] >= 0 && i > 0;
            const quint32 offset = isUsed ? quint32(offsets[size_t(i)]) : 0;
            rows.append(char(isUsed ? 1 : 0));
            for (int shift = 24; shift >= 0; shift -= 8)
            {
                rows.append(char((offset >> shift) & 0xFF));
            }
            rows.append(char(isUsed ? 0 : 0xFF));
            rows.append(char(isUsed ? 0 : 0xFF));
        }
        data += QByteArray::number(xrefNumber) + " 0 obj\n<< /Type /XRef /Size " + QByteArray::number(size) + " /W [1 4 2] /Root 1 0 R " + m_trailerEntries +
                " /Length " + QByteArray::number(rows.size()) + " >>\nstream\n" + rows + "\nendstream\nendobj\n";
        data += "startxref\n" + QByteArray::number(offsets[size_t(xrefNumber)]) + "\n%%EOF\n";
        return data;
    }

    pdf::PDFDocument read() const
    {
        return readBytes(write());
    }

    pdf::PDFDocumentPointer readPointer() const
    {
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(read()));
    }

    /// Reads bytes of a file (with a password, if it is encrypted; \p isOwner: the password is the
    /// owner password, an empty user password does not open the document as its user)
    static pdf::PDFDocument readBytes(const QByteArray& data, const QString& password = QString(), bool* isRead = nullptr, bool isOwner = false)
    {
        bool isFirstAttempt = true;
        auto callback = [&password, &isFirstAttempt](bool* ok) -> QString
        {
            *ok = isFirstAttempt;
            isFirstAttempt = false;
            return password;
        };
        pdf::PDFDocumentReader reader(nullptr, callback, false, isOwner);
        pdf::PDFDocument document = reader.readFromBuffer(data);
        if (isRead)
        {
            *isRead = reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK;
        }
        else if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            qWarning() << "The test document cannot be read:" << reader.getErrorMessage();
        }
        return document;
    }

    /// The bytes PDFDocumentWriter writes for the document
    static QByteArray writeDocument(const pdf::PDFDocument& document)
    {
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QBuffer::WriteOnly);
        pdf::PDFDocumentWriter writer(nullptr);
        if (!writer.write(&buffer, &document))
        {
            qWarning() << "The test document cannot be written";
        }
        return data;
    }

    /// The document written and read again (what a reader of the saved file sees)
    static pdf::PDFDocument roundTrip(const pdf::PDFDocument& document)
    {
        return readBytes(writeDocument(document));
    }

    /// The file of the document encrypted by AES 256 with the passwords and the permissions
    /// (the bits of PDFSecurityHandler::Permission, ISO 32000-2, Table 22)
    static QByteArray encrypt(const pdf::PDFDocument& document, const QString& userPassword, const QString& ownerPassword, uint32_t permissions)
    {
        pdf::PDFDocumentBuilder builder(&document);
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.userPassword = userPassword;
        settings.ownerPassword = ownerPassword;
        settings.permissions = permissions;
        builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        return writeDocument(builder.build());
    }

    /// A document of \p count pages (objects 3 ... 2 + count) below the root 2 with the media
    /// box 600 x 800 and empty resources; the content of the page n (object 100 + n, from 1)
    /// draws a line and has the comment "% <prefix>PAGE<n>"; the catalog 1 has \p catalogEntries,
    /// the page n the extra entries \p pageEntries[n] (from 1)
    static MergeTestPdf createPages(const QByteArray& prefix, int count, const QByteArray& catalogEntries = QByteArray(), const std::map<int, QByteArray>& pageEntries = {})
    {
        MergeTestPdf pdf;
        pdf.set(1, "<< /Type /Catalog /Pages 2 0 R " + catalogEntries + " >>");
        QByteArray kids;
        for (int page = 1; page <= count; ++page)
        {
            kids += QByteArray::number(page + 2) + " 0 R ";
            auto it = pageEntries.find(page);
            pdf.set(page + 2, "<< /Type /Page /Parent 2 0 R /Contents " + QByteArray::number(page + 100) + " 0 R " + (it != pageEntries.end() ? it->second : QByteArray()) + " >>");
            pdf.set(page + 100, stream("% " + prefix + "PAGE" + QByteArray::number(page) + "\n0 0 m 10 10 l S"));
        }
        pdf.set(2, "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(count) + " /MediaBox [0 0 600 800] /Resources << >> >>");
        return pdf;
    }

    /// Reference of the page n (from 1) of createPages, as text
    static QByteArray page(int number)
    {
        return QByteArray::number(number + 2) + " 0 R";
    }

private:
    std::map<int, QByteArray> m_objects;
    QByteArray m_trailerEntries;
    QByteArray m_version = "1.7";
};

/// Readers of the objects of a result
class MergeTestRead
{
public:
    static const pdf::PDFObject& object(const pdf::PDFDocument& document, const pdf::PDFObject& object)
    {
        return document.getObject(object);
    }

    static const pdf::PDFDictionary* dictionary(const pdf::PDFDocument& document, const pdf::PDFObject& object)
    {
        return document.getDictionaryFromObject(object);
    }

    static const pdf::PDFDictionary* catalog(const pdf::PDFDocument& document)
    {
        const pdf::PDFDictionary* trailerDictionary = trailer(document);
        return trailerDictionary ? document.getDictionaryFromObject(trailerDictionary->get("Root")) : nullptr;
    }

    /// The trailer (the dictionary of a cross-reference stream too, 7.5.8.2)
    static const pdf::PDFDictionary* trailer(const pdf::PDFDocument& document)
    {
        return document.getStorage().getDictionaryFromObject(document.getStorage().getTrailerDictionary());
    }

    /// The dictionary at the path of keys from the catalog ("AcroForm", "DR", "Font")
    static const pdf::PDFDictionary* path(const pdf::PDFDocument& document, std::initializer_list<const char*> keys)
    {
        const pdf::PDFDictionary* current = catalog(document);
        for (const char* key : keys)
        {
            if (!current)
            {
                return nullptr;
            }
            current = dictionary(document, current->get(key));
        }
        return current;
    }

    /// The items of an array (an empty list for another object)
    static std::vector<pdf::PDFObject> items(const pdf::PDFDocument& document, const pdf::PDFObject& array)
    {
        std::vector<pdf::PDFObject> result;
        const pdf::PDFObject& dereferenced = document.getObject(array);
        if (dereferenced.isArray())
        {
            for (size_t i = 0; i < dereferenced.getArray()->getCount(); ++i)
            {
                result.push_back(dereferenced.getArray()->getItem(i));
            }
        }
        return result;
    }

    static QString text(const pdf::PDFDocument& document, const pdf::PDFObject& value)
    {
        const pdf::PDFObject& dereferenced = document.getObject(value);
        return dereferenced.isString() ? pdf::PDFEncoding::convertTextString(dereferenced.getString()) : QString();
    }

    static QByteArray name(const pdf::PDFDocument& document, const pdf::PDFObject& value)
    {
        const pdf::PDFObject& dereferenced = document.getObject(value);
        return dereferenced.isName() ? dereferenced.getString() : QByteArray();
    }

    static pdf::PDFInteger integer(const pdf::PDFDocument& document, const pdf::PDFObject& value, pdf::PDFInteger defaultValue = -1)
    {
        const pdf::PDFObject& dereferenced = document.getObject(value);
        return dereferenced.isInt() ? dereferenced.getInteger() : defaultValue;
    }

    /// Content of the pages of the document joined (the markers of createPages)
    static QByteArray pageContents(const pdf::PDFDocument& document)
    {
        QByteArray result;
        for (size_t i = 0; i < document.getCatalog()->getPageCount(); ++i)
        {
            const pdf::PDFDictionary* page = document.getDictionaryFromObject(pdf::PDFObject::createReference(document.getCatalog()->getPage(i)->getPageReference()));
            const pdf::PDFObject& contents = document.getObject(page->get("Contents"));
            if (contents.isStream())
            {
                result += *contents.getStream()->getContent() + "|";
            }
        }
        return result;
    }

    /// Number of occurrences of a text in the bytes
    static int count(const QByteArray& data, const QByteArray& text)
    {
        int result = 0;
        for (qsizetype position = data.indexOf(text); position >= 0; position = data.indexOf(text, position + 1))
        {
            ++result;
        }
        return result;
    }
};

}   // namespace mergetest

#endif // MERGETESTPDF_H
