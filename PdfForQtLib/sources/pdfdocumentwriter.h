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
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFDOCUMENTWRITER_H
#define PDFDOCUMENTWRITER_H

#include "pdfdocument.h"
#include "pdfprogress.h"
#include "pdfutils.h"

#include <QIODevice>

namespace pdf
{

/// Class used for writing PDF documents to the desired target device (or file,
/// buffer, etc.). If writing is not successful, then error message is returned.
class PDFFORQTLIBSHARED_EXPORT PDFDocumentWriter
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentWriter)

public:
    explicit inline PDFDocumentWriter(PDFProgress* progress) :
        m_progress(progress)
    {

    }

    PDFOperationResult write(const QString& fileName, const PDFDocument* document);
    PDFOperationResult write(QIODevice* device, const PDFDocument* document);

    /// Calculates document file size, as if it is written to the disk.
    /// No file is accessed by this function; document is written
    /// to fake stream, which counts operations. If error occurs, and
    /// size can't be determined, then -1 is returned.
    /// \param document Document
    static qint64 getDocumentFileSize(const PDFDocument* document);

private:
    void writeCRLF(QIODevice* device);
    void writeObjectHeader(QIODevice* device, PDFObjectReference reference);
    void writeObjectFooter(QIODevice* device);

    /// Progress indicator
    PDFProgress* m_progress;
};

}   // namespace pdf

#endif // PDFDOCUMENTWRITER_H
