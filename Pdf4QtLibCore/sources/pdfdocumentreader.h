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

#ifndef PDFDOCUMENTREADER_H
#define PDFDOCUMENTREADER_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfprogress.h"
#include "pdfxreftable.h"

#include <QMutex>
#include <QIODevice>

namespace pdf
{
class PDFXRefTable;
class PDFParsingContext;

/// This class is a reader of PDF document from various devices (file, io device,
/// byte buffer). This class doesn't throw exceptions, to check errors, use
/// appropriate functions.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentReader
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentReader)

public:
    explicit PDFDocumentReader(PDFProgress* progress, const std::function<QString(bool*)>& getPasswordCallback, bool permissive, bool authorizeOwnerOnly);

    constexpr inline PDFDocumentReader(const PDFDocumentReader&) = delete;
    constexpr inline PDFDocumentReader(PDFDocumentReader&&) = delete;
    constexpr inline PDFDocumentReader& operator=(const PDFDocumentReader&) = delete;
    constexpr inline PDFDocumentReader& operator=(PDFDocumentReader&&) = delete;

    enum class Result
    {
        OK,         ///< Document was successfully loaded
        Failed,     ///< Error occured during document reading
        Cancelled   ///< User cancelled document reading
    };

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

    /// Returns result code for reading document from the device
    Result getReadingResult() const { return m_result; }

    /// Returns error message, if document reading was unsuccessfull
    const QString& getErrorMessage() const { return m_errorMessage; }

    /// Get source data of the document
    const QByteArray& getSource() const { return m_source; }

    /// Returns warning messages
    const QStringList& getWarnings() const { return m_warnings; }

    /// Returns true, if the document is not what the cross-reference table of the file says: its objects
    /// were restored from the bytes of a damaged file, which can hold objects no reader by the table shows,
    /// or an object of the table could not be read and it is missing. What is shown does not prove what was signed.
    bool isDocumentRestored() const { return m_isDocumentRestored; }

    /// Returns true, if the last objects of a damaged file are not a document, so its oldest ones are
    /// shown (isDocumentRestored is true then): the last changes of the file are not in the document.
    bool isOldestContentShown() const { return m_isOldestContentShown; }

    static QByteArray hash(const QByteArray& sourceData);

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

    void checkFooter(const QByteArray& buffer);
    void checkHeader(const QByteArray& buffer);
    PDFInteger findXrefTableOffset(const QByteArray& buffer);
    Result processReferenceTableEntries(PDFXRefTable* xrefTable, const std::vector<PDFXRefTable::Entry>& occupiedEntries, PDFObjectStorage::PDFObjects& objects);
    Result processSecurityHandler(const PDFObject& trailerDictionaryObject, const std::vector<PDFXRefTable::Entry>& occupiedEntries, PDFObjectStorage::PDFObjects& objects);
    void processObjectStreams(PDFXRefTable* xrefTable, PDFObjectStorage::PDFObjects& objects);

    /// This function fetches object from the buffer from the specified offset.
    /// Can throw exception, returns a pair of scanned reference and object content.
    /// \param context Context
    /// \param offset Offset
    /// \param reference Reference to parsed object
    PDFObject getObject(PDFParsingContext* context, PDFInteger offset, PDFObjectReference reference) const;

    /// Which of more objects of one reference, which can be read, wins in a damaged file
    enum class RestoredRevision
    {
        Last,   ///< The last one in the file, as in the order of incremental updates
        First   ///< The first one in the file, when the last ones are not a document
    };

    /// Restores objects from the object list. The result does not depend on the threads. Streams can
    /// have length defined in a referred object: its winning object is used.
    /// \param restoredObjects Map of restored objects
    /// \param offsets Offsets, from which are objects being read, in the order of the file
    /// \param revision Which object of a reference wins
    void restoreObjects(std::map<PDFObjectReference, PDFObject>& restoredObjects, const std::vector<std::pair<int, int>>& offsets, RestoredRevision revision);

    /// Returns objects of the storage from the restored objects: one object per number, the highest generation
    /// \param restoredObjects Map of restored objects
    static PDFObjectStorage::PDFObjects createRestoredObjects(const std::map<PDFObjectReference, PDFObject>& restoredObjects);

    /// Returns true, if the catalog and the page tree of the restored objects can be read
    /// \param restoredObjects Map of restored objects
    /// \param trailerDictionaryObject Trailer dictionary
    bool isRestoredDocumentReadable(const std::map<PDFObjectReference, PDFObject>& restoredObjects, const PDFObject& trailerDictionaryObject) const;

    /// Fetch object from reference table
    PDFObject getObjectFromXrefTable(PDFXRefTable* xrefTable, PDFParsingContext* context, PDFObjectReference reference) const;

    /// Tries to read damaged trailer dictionary: an entry of the trailer of the given revision
    /// wins (ISO 32000-2, 7.5.6). The bytes of a trailer inside of the objects (a stream, a
    /// string) are used only when no other trailer has the catalog.
    /// \param offsets Offsets of the objects, in the order of the file
    /// \param revision Which trailer of an entry wins
    PDFObject readDamagedTrailerDictionary(const std::vector<std::pair<int, int>>& offsets, RestoredRevision revision) const;

    /// Attempts to read a damaged PDF document from the specified buffer (byte array). If incorrect
    /// PDF is read, then empty PDF document is returned. No exception is thrown.
    PDFDocument readDamagedDocumentFromBuffer(const QByteArray& buffer);

    /// Version of a file read from its bytes, without the limits of a valid file (a damaged
    /// file can have any bytes before its header): the first header of a valid version,
    /// PDFVersion::getFallback when there is none. Never throws.
    /// \param buffer Bytes of the file
    static PDFVersion findDamagedFileVersion(const QByteArray& buffer);

    /// This function is used, when damaged pdf document is being restored. It returns
    /// array of hints, where objects should appear, in the order of the file. It constists of pair of
    /// start offset, and end offset. Start offset is always a valid index to the buffer, end offset
    /// can be one index after the buffers end (it is end iterator). An object ends by endobj or
    /// at the next object; the bytes of a stream or of a string are no objects.
    /// \param buffer Buffer
    std::vector<std::pair<int, int>> findObjectByteOffsets(const QByteArray& buffer) const;

    void progressStart(size_t stepCount, QString text);
    void progressStep();
    void progressFinish();

    /// Mutex for access to variables of this reader from more threads
    /// (providing thread safety)
    QMutex m_mutex;

    /// Result of document reading from the device
    std::atomic<Result> m_result;

    /// In case if error occurs, it is stored here
    QString m_errorMessage;

    /// Version of the scanned file
    PDFVersion m_version;

    /// Callback to obtain password from the user
    std::function<QString(bool*)> m_getPasswordCallback;

    /// Progress indicator
    PDFProgress* m_progress;

    /// Raw document data (byte array containing source data for created document)
    QByteArray m_source;

    /// Security handler
    PDFSecurityHandlerPointer m_securityHandler;

    /// Be permissive when reading, tolerate errors and try to fix broken document
    bool m_permissive;

    /// Authorize as owner only (if owner authorization fails, then whole document
    /// reading fails)
    bool m_authorizeOwnerOnly;

    /// Warnings
    QStringList m_warnings;

    /// Objects were restored from the bytes of the file
    bool m_isDocumentRestored = false;

    /// The objects of the first revision of the file were restored
    bool m_isOldestContentShown = false;
};

}   // namespace pdf

#endif // PDFDOCUMENTREADER_H
