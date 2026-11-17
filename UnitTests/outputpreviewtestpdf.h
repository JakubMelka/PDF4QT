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

#ifndef OUTPUTPREVIEWTESTPDF_H
#define OUTPUTPREVIEWTESTPDF_H

// The test documents of UnitTestsOutputPreview and UnitTestsInkCoverage (step 7 of phase 2, P3a): a
// PDF written from the texts of its objects, so every color space, group, overprint parameter and
// annotation is exactly as the test says (the expected values come from ISO 32000-2, never from a run).

#include "pdfdocument.h"
#include "pdfdocumentreader.h"

#include <QByteArray>
#include <QDebug>

#include <map>
#include <vector>

/// A test document: the catalog is object 1, the page tree object 2, the pages and the other objects
/// get their numbers from 3 in the order they are added
class OutputPreviewTestPdf
{
public:
    /// A page 100 x 100 points (one point is one pixel at the resolution 100) by default
    static constexpr const char* DEFAULT_MEDIA_BOX = "[0 0 100 100]";

    /// A number for an object, which is set later (a forward reference)
    int reserve() { return m_nextNumber++; }

    /// Sets the text of an object
    void set(int number, const QByteArray& text) { m_objects[number] = text; }

    /// Adds an object, returns its number
    int add(const QByteArray& text)
    {
        const int number = reserve();
        set(number, text);
        return number;
    }

    /// The text of a stream object with its length
    static QByteArray stream(const QByteArray& content, const QByteArray& entries = QByteArray())
    {
        return "<< /Length " + QByteArray::number(content.size()) + " " + entries + " >>\nstream\n" + content + "\nendstream";
    }

    /// The reference text "n 0 R"
    static QByteArray ref(int number) { return QByteArray::number(number) + " 0 R"; }

    /// Adds a page with its content stream, its resources and other entries of the page dictionary
    int addPage(const QByteArray& content, const QByteArray& resources = "<< >>", const QByteArray& pageEntries = QByteArray(), const QByteArray& mediaBox = DEFAULT_MEDIA_BOX)
    {
        const int contentNumber = add(stream(content));
        const int pageNumber = add("<< /Type /Page /Parent 2 0 R /MediaBox " + mediaBox + " /Contents " + ref(contentNumber) + " /Resources " + resources + " " + pageEntries + " >>");
        m_pages.push_back(pageNumber);
        return pageNumber;
    }

    /// The bytes of the file
    QByteArray write() const
    {
        std::map<int, QByteArray> objects = m_objects;
        objects[1] = "<< /Type /Catalog /Pages 2 0 R >>";

        QByteArray kids;
        for (const int page : m_pages)
        {
            kids += ref(page) + " ";
        }
        objects[2] = "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(qsizetype(m_pages.size())) + " >>";

        QByteArray data = "%PDF-1.7\n";
        const int size = objects.rbegin()->first + 1;
        std::vector<qint64> offsets(size_t(size), -1);
        for (const auto& [number, text] : objects)
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
        data += "trailer\n<< /Size " + QByteArray::number(size) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return data;
    }

    /// The parsed document
    pdf::PDFDocument read() const
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        pdf::PDFDocument document = reader.readFromBuffer(write());
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            qWarning() << "The test document cannot be read:" << reader.getErrorMessage();
        }
        return document;
    }

    /// The tint transformation of a spot ink into DeviceCMYK (ISO 32000-2, 7.10.3, an exponential
    /// function): tint 1 is the CMYK color \p cmyk ("0 1 0 0")
    static QByteArray tintFunction(const QByteArray& cmyk)
    {
        return "<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] /C1 [" + cmyk + "] /N 1 >>";
    }

    /// A Separation color space of a spot ink with an alternate DeviceCMYK
    static QByteArray separation(const QByteArray& name, const QByteArray& cmyk = "0 1 0 0")
    {
        return "[/Separation /" + name + " /DeviceCMYK " + tintFunction(cmyk) + "]";
    }

private:
    std::map<int, QByteArray> m_objects;
    std::vector<int> m_pages;
    int m_nextNumber = 3;
};

#endif // OUTPUTPREVIEWTESTPDF_H
