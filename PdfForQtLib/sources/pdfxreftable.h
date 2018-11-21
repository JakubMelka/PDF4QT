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
        Free,       ///< Entry represents a free item (no object)
        Occupied    ///< Entry represents a occupied item (object)
    };

    struct Entry
    {
        PDFObjectReference reference;
        PDFInteger offset = -1;
        EntryType type = EntryType::Free;
    };

    /// Tries to read reference table from the byte array. If error occurs, then exception
    /// is raised. This fuction also checks redundant entries.
    /// \param context Current parsing context
    /// \param byteArray Input byte array (containing the PDF file)
    /// \param startTableOffset Offset of first reference table
    void readXRefTable(PDFParsingContext* context, const QByteArray& byteArray, PDFInteger startTableOffset);

private:
    /// Reference table entries
    std::vector<Entry> m_entries;

    /// Trailer dictionary
    PDFObject m_trailerDictionary;
};

}   // namespace pdf

#endif // PDFXREFTABLE_H
