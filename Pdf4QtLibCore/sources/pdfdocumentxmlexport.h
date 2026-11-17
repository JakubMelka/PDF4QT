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

#ifndef PDFDOCUMENTXMLEXPORT_H
#define PDFDOCUMENTXMLEXPORT_H

#include "pdfglobal.h"
#include "pdfdocument.h"

#include <QCoreApplication>
#include <QString>

class QIODevice;

namespace pdf
{
class PDFOperationControl;
class PDFProgressSink;

/// The structure of a document as XML (Document Report, kind Structure as XML, 484:18196; PdfTool xml):
/// the trailer and every object of the storage as a flat list, the references are written, never followed
/// (plan of step 7 of phase 2, 4.4, 6.1). The move of PDFXmlExportVisitor of PdfTool (pdftoolxml.cpp), one
/// implementation for PdfTool, the tool and MCP.
///
/// Contract (package P1b implements it; the skeleton has a stub, which fails):
///  - the four options of 484:18196 (the streams, the streams as text, the indentation, the strings always
///    binary); the strings and the streams of an encrypted document decrypted;
///  - the XML is written as a stream into \p device (a QSaveFile of the export in the job of the run, never
///    the whole file in the memory: a huge document); each stream decoded by PDFLimitedStreamDecoder with
///    the limit of the inspector, a stream over it is written as its error, never decoded in whole;
///  - every text written by QXmlStreamWriter with the characters not allowed in XML 1.0 replaced
///    (PDFDocumentToolTableWriter::toXmlText): the file is always well-formed;
///  - the comment of the generator is "PDF4QT <version>" (K7.21);
///  - the cancellation is checked after each object (\p control), a step per 1 % of the objects (\p progress).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentXmlExport
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentXmlExport)

public:
    PDFDocumentXmlExport() = delete;

    /// The options of 484:18196 (the parameters of review.report: exportStreams, streamsAsText, indent,
    /// alwaysBinaryStrings; PdfTool --xml-export-streams, --xml-export-streams-as-text, --xml-use-indent,
    /// --xml-always-binary)
    class Options
    {
    public:
        bool isStreamExported = false;          ///< The data of the streams are written
        bool isStreamWrittenAsText = false;     ///< The data of the streams as a text (else hexadecimal)
        bool isIndented = true;                 ///< The XML is indented
        bool isStringAlwaysBinary = false;      ///< Every string hexadecimal
    };

    /// Writes the XML of the document into the device
    /// \param document The document
    /// \param options The options
    /// \param device An open writable device
    /// \param control Cancellation (optional)
    /// \param progress The progress (optional)
    /// \param errorMessage Translated reason of a failure (optional)
    /// \returns true, when the whole XML was written
    static bool write(const PDFDocument* document, const Options& options, QIODevice* device, const PDFOperationControl* control, PDFProgressSink* progress, QString* errorMessage);

    /// An estimate of the size of the file in bytes (the dialog of the export, plan 4.4); -1 = unknown
    static qint64 estimateSize(const PDFDocument* document, const Options& options);
};

}   // namespace pdf

#endif // PDFDOCUMENTXMLEXPORT_H
