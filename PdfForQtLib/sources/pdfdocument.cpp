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


#include "pdfdocument.h"
#include "pdfparser.h"
#include "pdfencoding.h"

namespace pdf
{

// Entries for "Info" entry in trailer dictionary
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY = "Info";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_TITLE = "Title";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_AUTHOR = "Author";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_SUBJECT = "Subject";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_KEYWORDS = "Keywords";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_CREATOR = "Creator";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_PRODUCER = "Producer";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_CREATION_DATE = "CreationDate";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_MODIFIED_DATE = "ModDate";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_TRAPPED = "Trapped";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_TRAPPED_TRUE = "True";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_TRAPPED_FALSE = "False";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_TRAPPED_UNKNOWN = "Unknown";

void PDFDocument::init()
{
    initInfo();
}

void PDFDocument::initInfo()
{
    const PDFObject& trailerDictionary = m_pdfObjectStorage.getTrailerDictionary();

    // Trailer object should be dictionary here. It is verified in the document reader.
    Q_ASSERT(trailerDictionary.isDictionary());

    const PDFDictionary* dictionary = trailerDictionary.getDictionary();
    Q_ASSERT(dictionary);

    if (dictionary->hasKey(PDF_DOCUMENT_INFO_ENTRY))
    {
        const PDFObject& info = getObject(dictionary->get(PDF_DOCUMENT_INFO_ENTRY));

        if (info.isDictionary())
        {
            const PDFDictionary* infoDictionary = info.getDictionary();
            Q_ASSERT(infoDictionary);

            auto readTextString = [this, infoDictionary](const char* entry, QString& fillEntry)
            {
                if (infoDictionary->hasKey(entry))
                {
                    const PDFObject& stringObject = getObject(infoDictionary->get(entry));
                    if (stringObject.isString())
                    {
                        // We have succesfully read the string, convert it according to encoding
                        fillEntry = PDFEncoding::convertTextString(stringObject.getString());
                    }
                    else if (!stringObject.isNull())
                    {
                        throw PDFParserException(tr("Bad format of document info entry in trailer dictionary. String expected."));
                    }
                }
            };
            readTextString(PDF_DOCUMENT_INFO_ENTRY_TITLE, m_info.title);
            readTextString(PDF_DOCUMENT_INFO_ENTRY_AUTHOR, m_info.author);
            readTextString(PDF_DOCUMENT_INFO_ENTRY_SUBJECT, m_info.subject);
            readTextString(PDF_DOCUMENT_INFO_ENTRY_KEYWORDS, m_info.keywords);
            readTextString(PDF_DOCUMENT_INFO_ENTRY_CREATOR, m_info.creator);
            readTextString(PDF_DOCUMENT_INFO_ENTRY_PRODUCER, m_info.producer);

            auto readDate= [this, infoDictionary](const char* entry, QDateTime& fillEntry)
            {
                if (infoDictionary->hasKey(entry))
                {
                    const PDFObject& stringObject = getObject(infoDictionary->get(entry));
                    if (stringObject.isString())
                    {
                        // We have succesfully read the string, convert it to date time
                        fillEntry = PDFEncoding::convertToDateTime(stringObject.getString());

                        if (!fillEntry.isValid())
                        {
                            throw PDFParserException(tr("Bad format of document info entry in trailer dictionary. String with date time format expected."));
                        }
                    }
                    else if (!stringObject.isNull())
                    {
                        throw PDFParserException(tr("Bad format of document info entry in trailer dictionary. String with date time format expected."));
                    }
                }
            };
            readDate(PDF_DOCUMENT_INFO_ENTRY_CREATION_DATE, m_info.creationDate);
            readDate(PDF_DOCUMENT_INFO_ENTRY_MODIFIED_DATE, m_info.modifiedDate);

            if (infoDictionary->hasKey(PDF_DOCUMENT_INFO_ENTRY_TRAPPED))
            {
                const PDFObject& nameObject = getObject(infoDictionary->get(PDF_DOCUMENT_INFO_ENTRY_TRAPPED));
                if (nameObject.isName())
                {
                    const QByteArray& name = nameObject.getString();
                    if (name == PDF_DOCUMENT_INFO_ENTRY_TRAPPED_TRUE)
                    {
                        m_info.trapped = Info::Trapped::True;
                    }
                    else if (name == PDF_DOCUMENT_INFO_ENTRY_TRAPPED_FALSE)
                    {
                        m_info.trapped = Info::Trapped::False;
                    }
                    else if (name == PDF_DOCUMENT_INFO_ENTRY_TRAPPED_UNKNOWN)
                    {
                        m_info.trapped = Info::Trapped::Unknown;
                    }
                    else
                    {
                        throw PDFParserException(tr("Bad format of document info entry in trailer dictionary. Trapping information expected"));
                    }
                }
                else
                {
                    throw PDFParserException(tr("Bad format of document info entry in trailer dictionary. Trapping information expected"));
                }
            }
        }
        else if (!info.isNull()) // Info may be invalid...
        {
            throw PDFParserException(tr("Bad format of document info entry in trailer dictionary."));
        }
    }
}

const PDFObject& PDFObjectStorage::getObject(PDFObjectReference reference) const
{
    if (reference.objectNumber >= 0 &&
        reference.objectNumber < static_cast<PDFInteger>(m_objects.size()) &&
        m_objects[reference.objectNumber].generation == reference.generation)
    {
        return m_objects[reference.objectNumber].object;
    }
    else
    {
        static const PDFObject dummy;
        return dummy;
    }
}

}   // namespace pdf
