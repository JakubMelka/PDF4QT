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


#ifndef PDFDOCUMENTREADER_H
#define PDFDOCUMENTREADER_H

#include "pdfglobal.h"
#include "pdfdocument.h"

#include <QtCore>
#include <QIODevice>

namespace pdf
{

/// This class is a reader of PDF document from various devices (file, io device,
/// byte buffer). This class doesn't throw exceptions, to check errors, use
/// appropriate functions.
class PDFFORQTLIBSHARED_EXPORT PDFDocumentReader
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentReader)

public:
    explicit PDFDocumentReader();

    constexpr inline PDFDocumentReader(const PDFDocumentReader&) = delete;
    constexpr inline PDFDocumentReader(PDFDocumentReader&&) = delete;
    constexpr inline PDFDocumentReader& operator=(const PDFDocumentReader&) = delete;
    constexpr inline PDFDocumentReader& operator=(PDFDocumentReader&&) = delete;

    /// Reads a PDF document from the specified file. If file doesn't exist,
    /// cannot be opened or contain invalid pdf, empty PDF file is returned.
    /// No exception is thrown.
    PDFDocument readFromFile(const QString& fileName);

    /// Reads a PDF document from the specified device. If device is not opened
    /// for reading, then function tries it to open for reading. If it is opened,
    /// but not for reading, empty PDF document is returned. This also occurs
    /// when incorrect PDF is read. No exception is thrown.
    PDFDocument readFromDevice(QIODevice* device);

    /// Reads a PDF document from the specified buffer (byte array). If incorrect
    /// PDF is read, then empty PDF document is returned. No exception is thrown.
    PDFDocument readFromBuffer(const QByteArray& buffer);

    /// Returns true, if document was successfully read from device
    bool isSuccessfull() const { return m_successfull; }

    /// Returns error message, if document reading was unsuccessfull
    const QString& getErrorMessage() const { return m_errorMessage; }

private:
    static constexpr const int FIND_NOT_FOUND_RESULT = -1;

    /// Resets the internal state and prepares it for new reading cycle
    void reset();

    /// Find a last string in the byte array, scan only \p limit bytes. If string
    /// is not found, then FIND_NOT_FOUND_RESULT is returned, if it is found, then
    /// it position from the beginning of byte array is returned.
    /// \param what String to be found
    /// \param byteArray Byte array to be scanned from the end
    /// \param limit Scan up to this value bytes from the end
    /// \returns Position of string, or FIND_NOT_FOUND_RESULT
    int findFromEnd(const char* what, const QByteArray& byteArray, int limit);

    /// This bool flag is set, if pdf document was successfully read from the device
    bool m_successfull;

    /// In case if error occurs, it is stored here
    QString m_errorMessage;

    /// Version of the scanned file
    PDFVersion m_version;
};

}   // namespace pdf

#endif // PDFDOCUMENTREADER_H
