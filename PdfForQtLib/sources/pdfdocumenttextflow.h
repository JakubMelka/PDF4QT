//    Copyright (C) 2020 Jakub Melka
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

#ifndef PDFDOCUMENTTEXTFLOW_H
#define PDFDOCUMENTTEXTFLOW_H

#include "pdfglobal.h"
#include "pdfexception.h"

namespace pdf
{
class PDFDocument;

/// Text flow extracted from document. Text flow can be created \p PDFDocumentTextFlowFactory.
/// Flow can contain various items, not just text ones. Also, some manipulation functions
/// are available, they can modify text flow by various content.
class PDFFORQTLIBSHARED_EXPORT PDFDocumentTextFlow
{
public:

    enum Flag
    {
        None                                = 0x0000,   ///< No text flag
        Text                                = 0x0001,   ///< Ordinary text
        PageStart                           = 0x0002,   ///< Page start marker
        PageEnd                             = 0x0004,   ///< Page end marker
        StructureTitle                      = 0x0008,   ///< Structure tree item title
        StructureLanguage                   = 0x0010,   ///< Structure tree item language
        StructureAlternativeDescription     = 0x0020,   ///< Structure tree item alternative description
        StructureExpandedForm               = 0x0040,   ///< Structure tree item expanded form of text
        StructureActualText                 = 0x0080,   ///< Structure tree item actual text
        StructurePhoneme                    = 0x0100,   ///< Structure tree item  phoneme
        StructureItemStart                  = 0x0200,   ///< Start of structure tree item
        StructureItemEnd                    = 0x0400,   ///< End of structure tree item
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    struct Item
    {
        Flags flags = None;
        PDFInteger pageIndex = 0;
        QString text;
    };
    using Items = std::vector<Item>;

    explicit PDFDocumentTextFlow() = default;
    explicit PDFDocumentTextFlow(Items&& items) :
        m_items(qMove(items))
    {

    }

    const Items& getItems() const { return m_items; }

private:
    Items m_items;
};

/// This factory creates text flow for whole document
class PDFFORQTLIBSHARED_EXPORT PDFDocumentTextFlowFactory
{
public:
    explicit PDFDocumentTextFlowFactory() = default;

    enum class Algorithm
    {
        Auto,       ///< Determine best text layout algorithm automatically
        Layout,     ///< Use text layout recognition using docstrum algorithm
        Content,    ///< Use content-stream text layout recognition (usually unreliable), but fast
        Structure,  ///< Use structure oriented text layout recognition (requires tagged document)
    };

    /// Performs document text flow analysis using given algorithm. Text flow
    /// can be performed only for given subset of pages, if required.
    /// \param document Document
    /// \param pageIndices Analyzed page indices
    /// \param algorithm Algorithm
    PDFDocumentTextFlow create(const PDFDocument* document,
                               const std::vector<PDFInteger>& pageIndices,
                               Algorithm algorithm);

    /// Has some error/warning occured during text layout creation?
    bool hasError() const { return !m_errors.isEmpty(); }

    /// Returns a list of errors/warnings
    const QList<PDFRenderError>& getErrors() const { return m_errors; }

private:
    QList<PDFRenderError> m_errors;
};

}   // namespace pdf

#endif // PDFDOCUMENTTEXTFLOW_H
