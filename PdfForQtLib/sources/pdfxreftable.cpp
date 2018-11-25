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

#include "pdfxreftable.h"
#include "pdfconstants.h"
#include "pdfparser.h"

#include <stack>

namespace pdf
{

void PDFXRefTable::readXRefTable(PDFParsingContext* context, const QByteArray& byteArray, PDFInteger startTableOffset)
{
    PDFParser parser(byteArray, context, PDFParser::None);

    m_entries.clear();

    std::set<PDFInteger> processedOffsets;
    std::stack<PDFInteger> workSet;
    workSet.push(startTableOffset);

    while (!workSet.empty())
    {
        PDFInteger currentOffset = workSet.top();
        workSet.pop();

        // Check, if we have cyclical references between tables
        if (processedOffsets.count(currentOffset))
        {
            throw PDFParserException(tr("Cyclic reference found in reference table."));
        }
        else
        {
            processedOffsets.insert(currentOffset);
        }

        // Now, we are ready to scan the table. Seek to the start of the reference table.
        parser.seek(currentOffset);

        if (parser.fetchCommand(PDF_XREF_HEADER))
        {
            while (!parser.fetchCommand(PDF_XREF_TRAILER))
            {
                // Now, first number is start offset, second number is count of table items
                PDFObject firstObject = parser.getObject();
                PDFObject countObject = parser.getObject();

                if (!firstObject.isInt() || !countObject.isInt())
                {
                    throw PDFParserException(tr("Invalid format of reference table."));
                }

                PDFInteger firstObjectNumber = firstObject.getInteger();
                PDFInteger count = countObject.getInteger();

                const PDFInteger lastObjectIndex = firstObjectNumber + count - 1;
                const PDFInteger desiredSize = lastObjectIndex + 1;

                if (static_cast<PDFInteger>(m_entries.size()) < desiredSize)
                {
                    m_entries.resize(desiredSize);
                }

                // Now, read the records
                for (PDFInteger i = 0; i < count; ++i)
                {
                    const PDFInteger objectNumber = firstObjectNumber + i;

                    PDFObject offset = parser.getObject();
                    PDFObject generation = parser.getObject();

                    bool occupied = parser.fetchCommand(PDF_XREF_OCCUPIED);
                    if (!occupied && !parser.fetchCommand(PDF_XREF_FREE))
                    {
                        throw PDFParserException(tr("Bad format of reference table entry."));
                    }

                    if (!offset.isInt() || !generation.isInt())
                    {
                        throw PDFParserException(tr("Bad format of reference table entry."));
                    }

                    Entry entry;
                    if (occupied)
                    {
                        entry.reference = PDFObjectReference(objectNumber, generation.getInteger());
                        entry.offset = offset.getInteger();
                        entry.type = EntryType::Occupied;
                    }

                    if (m_entries[objectNumber].type == EntryType::Free)
                    {
                        m_entries[objectNumber] = std::move(entry);
                    }
                }
            }

            PDFObject trailerDictionary = parser.getObject();
            if (!trailerDictionary.isDictionary())
            {
                throw PDFParserException(tr("Trailer dictionary is invalid."));
            }

            // Now, we have scanned the table. If we didn't have a trailer dictionary yet, then
            // try to load it. We must also check, that trailer dictionary is OK.
            if (m_trailerDictionary.isNull())
            {
                m_trailerDictionary = trailerDictionary;
            }

            const PDFDictionary* dictionary = trailerDictionary.getDictionary();
            if (dictionary->hasKey(PDF_XREF_TRAILER_PREVIOUS))
            {
                PDFObject previousOffset = dictionary->get(PDF_XREF_TRAILER_PREVIOUS);

                if (!previousOffset.isInt())
                {
                    throw PDFParserException(tr("Offset of previous reference table is invalid."));
                }

                workSet.push(previousOffset.getInteger());
            }

            if (dictionary->hasKey(PDF_XREF_TRAILER_XREFSTM))
            {
                throw PDFParserException(tr("Hybrid reference tables not supported."));
            }
        }
        else
        {
            throw PDFParserException(tr("Invalid format of reference table."));
        }
    }
}

std::vector<PDFXRefTable::Entry> PDFXRefTable::getOccupiedEntries() const
{
    std::vector<PDFXRefTable::Entry> result;

    // Suppose majority of items are occupied
    result.reserve(m_entries.size());
    std::copy_if(m_entries.cbegin(), m_entries.cend(), std::back_inserter(result), [](const Entry& entry) { return entry.type == EntryType::Occupied; });

    return result;
}

const PDFXRefTable::Entry& PDFXRefTable::getEntry(PDFObjectReference reference) const
{
    // We must also check generation number here. For this reason, we compare references of the entry at given position.
    if (reference.objectNumber >= 0 && reference.objectNumber < m_entries.size() && m_entries[reference.objectNumber].reference == reference)
    {
        return m_entries[reference.objectNumber];
    }
    else
    {
        static Entry dummy;
        return dummy;
    }
}

}   // namespace pdf
