//    Copyright (C) 2018-2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFXREFTABLE_H
#define PDFXREFTABLE_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QtCore>

#include <vector>

namespace pdf
{
class PDFParsingContext;

/// Represents table of references in the PDF file. It contains
/// scanned table in the PDF file, together with information, if entry
/// is occupied, or it is free.
class PDFXRefTable
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFXRefTable)

public:
    constexpr inline explicit PDFXRefTable() = default;

    // Enforce default copy constructor and default move constructor
    constexpr inline PDFXRefTable(const PDFXRefTable&) = default;
    constexpr inline PDFXRefTable(PDFXRefTable&&) = default;

    // Enforce default copy assignment operator and move assignment operator
    constexpr inline PDFXRefTable& operator=(const PDFXRefTable&) = default;
    constexpr inline PDFXRefTable& operator=(PDFXRefTable&&) = default;

    enum class EntryType
    {
        Free,           ///< Entry represents a free item (no object)
        Occupied,       ///< Entry represents a occupied item (object)
        InObjectStream  ///< Entry in object stream
    };

    struct Entry
    {
        PDFObjectReference reference;
        PDFObjectReference objectStream;
        PDFInteger offset = -1;
        PDFInteger indexInObjectStream = -1;
        EntryType type = EntryType::Free;
    };

    /// Tries to read reference table from the byte array. If error occurs, then exception
    /// is raised. This fuction also checks redundant entries.
    /// \param context Current parsing context
    /// \param byteArray Input byte array (containing the PDF file)
    /// \param startTableOffset Offset of first reference table
    void readXRefTable(PDFParsingContext* context, const QByteArray& byteArray, PDFInteger startTableOffset);

    /// Filters only occupied entries and returns them
    std::vector<Entry> getOccupiedEntries() const;

    /// Filters only object stream entries and returns them
    std::vector<Entry> getObjectStreamEntries() const;

    /// Returns size of the reference table
    std::size_t getSize() const { return m_entries.size(); }

    /// Gets the entry for given reference. If entry for given reference is not found,
    /// then free entry is returned.
    const Entry& getEntry(PDFObjectReference reference) const;

    /// Returns the trailer dictionary
    const PDFObject& getTrailerDictionary() const { return m_trailerDictionary; }

private:
    /// Reference table entries
    std::vector<Entry> m_entries;

    /// Trailer dictionary
    PDFObject m_trailerDictionary;
};

}   // namespace pdf

#endif // PDFXREFTABLE_H
