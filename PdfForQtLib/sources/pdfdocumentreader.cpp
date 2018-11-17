//    Copyright (C) 2018 Jakub Melka
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


#include "pdfdocumentreader.h"
#include "pdfparser.h"
#include "pdfconstants.h"

#include <QFile>

#include <regex>
#include <cctype>
#include <algorithm>

namespace pdf
{

PDFDocumentReader::PDFDocumentReader() :
    m_successfull(true)
{

}

PDFDocument PDFDocumentReader::readFromFile(const QString& fileName)
{
    QFile file(fileName);

    reset();

    if (file.exists())
    {
        if (file.open(QFile::ReadOnly))
        {
            PDFDocument document = readFromDevice(&file);
            file.close();
            return document;
        }
        else
        {
            m_successfull = false;
            m_errorMessage = tr("File '%1' cannot be opened for reading. %1").arg(file.errorString());
        }
    }
    else
    {
        m_successfull = false;
        m_errorMessage = tr("File '%1' doesn't exist.").arg(fileName);
    }

    return PDFDocument();
}

PDFDocument PDFDocumentReader::readFromDevice(QIODevice* device)
{
    reset();

    if (device->isOpen())
    {
        if (device->isReadable())
        {
            // Do not close the device, it was not opened by us.
            return readFromBuffer(device->readAll());
        }
        else
        {
            m_successfull = false;
            m_errorMessage = tr("Device is not opened for reading.");
        }
    }
    else if (device->open(QIODevice::ReadOnly))
    {
        QByteArray byteArray = device->readAll();
        device->close();
        return readFromBuffer(byteArray);
    }
    else
    {
        m_successfull = false;
        m_errorMessage = tr("Can't open device for reading.");
    }

    return PDFDocument();
}

PDFDocument PDFDocumentReader::readFromBuffer(const QByteArray& buffer)
{
    try
    {
        // FOOTER CHECKING
        //  1) Check, if EOF marking is present
        //  2) Find start of cross reference table
        if (findFromEnd(PDF_END_OF_FILE_MARK, buffer, PDF_FOOTER_SCAN_LIMIT) == FIND_NOT_FOUND_RESULT)
        {
            throw PDFParserException(tr("End of file marking was not found."));
        }

        const int startXRefPosition = findFromEnd(PDF_START_OF_XREF_MARK, buffer, PDF_FOOTER_SCAN_LIMIT);
        if (startXRefPosition == FIND_NOT_FOUND_RESULT)
        {
            throw PDFParserException(tr("Start of object reference table not found."));
        }

        // HEADER CHECKING
        //  1) Check if header is present
        //  2) Scan header version

        // According to PDF Reference 1.7, Appendix H, file header can have two formats:
        //  - %PDF-x.x
        //  - %!PS-Adobe-y.y PDF-x.x
        // We will search for both of these formats.

        std::regex headerRegExp("(%PDF-[[:digit:]]\\.[[:digit:]])|(%!PS-Adobe-[[:digit:]]\\.[[:digit:]] PDF-[[:digit:]]\\.[[:digit:]])");
        std::cmatch headerMatch;

        auto itBegin = buffer.cbegin();
        auto itEnd = std::next(buffer.cbegin(), qMin(buffer.size(), PDF_HEADER_SCAN_LIMIT));

        if (std::regex_search(itBegin, itEnd, headerMatch, headerRegExp))
        {
            // Size depends on regular expression, not on the text (if regular expresion is matched)
            Q_ASSERT(headerMatch.size() == 3);
            Q_ASSERT(headerMatch[1].matched != headerMatch[2].matched);

            for (int i : { 1, 2 })
            {
                if (headerMatch[i].matched)
                {
                    Q_ASSERT(std::distance(headerMatch[i].first, headerMatch[i].second) == 3);
                    m_version = PDFVersion(*headerMatch[i].first - '0', *std::prev(headerMatch[i].second) - '0');
                    break;
                }
            }
        }
        else
        {
            throw PDFParserException(tr("Header of PDF file was not found."));
        }

        // Check, if version is valid
        if (!m_version.isValid())
        {
            throw PDFParserException(tr("Version of the PDF file is not valid."));
        }


    }
    catch (PDFParserException parserException)
    {
        m_successfull = false;
        m_errorMessage = parserException.getMessage();
    }

    return PDFDocument();
}

void PDFDocumentReader::reset()
{
    m_successfull = true;
    m_errorMessage = QString();
    m_version = PDFVersion();
}

int PDFDocumentReader::findFromEnd(const char* what, const QByteArray& byteArray, int limit)
{
    if (byteArray.isEmpty())
    {
        // Byte array is empty, no value found
        return FIND_NOT_FOUND_RESULT;
    }

    const int size = byteArray.size();
    const int adjustedLimit = qMin(byteArray.size(), limit);
    const int whatLength = static_cast<int>(std::strlen(what));

    if (adjustedLimit < whatLength)
    {
        // Buffer is smaller than scan string
        return FIND_NOT_FOUND_RESULT;
    }

    auto itBegin = std::next(byteArray.cbegin(), size - adjustedLimit);
    auto itEnd = byteArray.cend();
    auto it = std::find_end(itBegin, itEnd, what, std::next(what, whatLength));

    if (it != byteArray.cend())
    {
        return std::distance(byteArray.cbegin(), it);
    }

    return FIND_NOT_FOUND_RESULT;
}

}   // namespace pdf
