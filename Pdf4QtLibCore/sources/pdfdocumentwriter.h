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
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentWriter
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentWriter)

public:
    explicit inline PDFDocumentWriter(PDFProgressSink* progress)
    {
        Q_UNUSED(progress);
    }

    /// Writes document to the file. If \p safeWrite is true, then document is first
    /// written to the temporary file, and then renamed to original file name atomically,
    /// so no data can be lost on, for example, power failure. If it is not possible to
    /// create temporary file, then writing operation will attempt to write to the file
    /// directly.
    /// \param fileName File name
    /// \param document Document
    /// \param safeWrite Write document to the temporary file and then rename
    PDFOperationResult write(const QString& fileName, const PDFDocument* document, bool safeWrite);

    /// Write document to the output device. Device must be writable (i.e. opened
    /// for writing).
    /// \param device Output device
    /// \param document Document
    PDFOperationResult write(QIODevice* device, const PDFDocument* document);

    /// Writes the document as an incremental update of the original data. The
    /// original data are written first, followed by the objects which differ
    /// from the original document and by a new cross-reference section linked
    /// to the previous one. The bytes of the original document are therefore
    /// preserved, including the byte ranges covered by its digital signatures.
    /// The document must be derived from the original data, so that the object
    /// numbers of the unchanged objects match. The format of the appended
    /// cross-reference section (classic table or stream) follows the format
    /// used by the original document.
    /// \param device Output device positioned at the start of the file
    /// \param originalData Data of the original document
    /// \param document Document to be written
    PDFOperationResult writeIncrementalUpdate(QIODevice* device, const QByteArray& originalData, const PDFDocument* document);

    /// Calculates document file size, as if it is written to the disk.
    /// No file is accessed by this function; document is written
    /// to fake stream, which counts operations. If error occurs, and
    /// size can't be determined, then -1 is returned.
    /// \param document Document
    static qint64 getDocumentFileSize(const PDFDocument* document);

    /// Calculates size estimate of an object. If object is null, then zero is returned.
    /// \param document Document
    /// \param reference Reference
    static qint64 getObjectSize(const PDFDocument* document, PDFObjectReference reference);

    /// Returns the text of a real number in a file: the shortest text in the fixed notation
    /// that reads back as the same double, always with a decimal point, so it stays a real
    /// object. It is ASCII and it does not depend on the locale. PDF has no exponential format
    /// (PDF Reference 1.7, chapter 3.2.2), so a very large or very small value has a long text.
    /// PDF has no NaN and no infinity, a computed value of them must not make the file invalid:
    /// NaN is 0.0 and an infinity is the largest real of Table C.1 with its sign.
    /// \param value Value
    static QByteArray getRealNumberText(PDFReal value);

    /// Writes an object to byte array, without object header/footer
    /// \param object Object to be written
    static QByteArray getSerializedObject(const PDFObject& object);

    /// What the encryption of a document needs of the file written whole (plan of step 5 of phase
    /// 2, B98, R5-16): the crypt filter V2 of V 4 (RC4 128-bit, revision 4) needs PDF 1.5, AESV2
    /// (AES 128-bit) PDF 1.6, AESV3 (V 5: revision 6, the public-key handler) PDF 2.0 or PDF 1.7
    /// with the developer extension /ADBE of /BaseVersion /1.7 and /ExtensionLevel 8 (3 for the
    /// deprecated revision 5). The header of the file is never lowered and never raised to 2.0
    /// (2.0 changes other rules of a document too): the extension goes into the catalog instead.
    struct EncryptionVersion
    {
        PDFVersion minimumVersion;      ///< The lowest version of the header (invalid = no requirement)
        int adbeExtensionLevel = 0;     ///< The level of the extension /ADBE needed below PDF 2.0 (0 = none)
    };

    /// The requirements of the encryption of the document (its security handler and its /Encrypt)
    /// \param document Document
    static EncryptionVersion getEncryptionVersion(const PDFDocument* document);

    /// True, if the document is encrypted as the original file: both without /Encrypt and with a
    /// handler of no encryption, or the same /Encrypt dictionary (object for object) and a handler
    /// of the same mode. An incremental update cannot change the encryption (plan of step 5 of
    /// phase 2, B80): the old revisions stay encrypted by the old key under the new /Encrypt.
    /// \param document The document to be written
    /// \param original The document read of the original data
    static bool hasSameEncryption(const PDFDocument* document, const PDFDocument* original);

private:
    /// Finds the last cross-reference section of the document data, to which
    /// the incremental update must be linked
    /// \param data Document data
    /// \param offset Offset of the section
    /// \param isCrossReferenceStream Section is a cross-reference stream (not a classic table)
    static bool findLastCrossReferenceSection(const QByteArray& data, PDFInteger& offset, bool& isCrossReferenceStream);

    static void writeCRLF(QIODevice* device);
    static void writeObjectHeader(QIODevice* device, PDFObjectReference reference);
    static void writeObjectFooter(QIODevice* device);
};

}   // namespace pdf

#endif // PDFDOCUMENTWRITER_H
