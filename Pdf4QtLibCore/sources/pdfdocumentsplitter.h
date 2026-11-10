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

#ifndef PDFDOCUMENTSPLITTER_H
#define PDFDOCUMENTSPLITTER_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfdocumentpageoperations.h"
#include "pdfimageoptimizer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

namespace pdf
{

class PDFOperationControl;
class PDFProgressSink;

/// Parts of one document as new documents (plan of step 3 of phase 2, 4.3, 5.6, R2, Q6): Split,
/// Extract Pages one file per page, the outputs of Manage Pages, PdfTool separate.
///
/// Each part is PDFDocumentPageOperations::keepPages over one index of the document (the rules
/// of removePages: the outline of all levels, the destinations, the links, the fields with their
/// widgets, the layers, the page labels - no object of another part reaches it; the structure
/// tree, the articles and the document actions are removed as by Remove Pages, owner answer Q5
/// a), finished by PDFNewDocumentFinisher (a new /ID, not encrypted, no /Perms, no declaration of
/// PDF/A or PDF/UA, the objects numbered anew - the cross-reference table of a part has only its
/// own objects), the images optimized when asked, and written to bytes. The attachments of the
/// document (/Names /EmbeddedFiles, /AF of the catalog) are kept only in the first part (owner
/// answer Q6); the attachments of the pages go with their pages.
///
/// Memory and threads (R2): the parts are made in windows of at most the threads of
/// PDFExecutionPolicy less one (the finishing runs its own parallel loops in the free thread;
/// one window when the images are optimized, whose loops are nested deeper), each window is
/// handed to the receiver in the order of the parts in the thread of the call, then released -
/// never all parts in memory. The receiver is called only from the thread of split.
/// Reentrant: the state of a split is local. Throws nothing: a failure is in the result.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentSplitter
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentSplitter)

public:
    PDFDocumentSplitter() = delete;

    /// One part: its pages (indices from 0, in the order of the document)
    struct Part
    {
        std::vector<PDFInteger> pages;
    };

    /// What a split does
    struct Settings
    {
        std::optional<PDFImageOptimizer::Settings> imageSettings;   ///< The images of each part are optimized (Optimize of Split, K24 of step 2); std::nullopt = Off
        QDateTime creationTime;                                     ///< Time of the creation of the parts, invalid = now
        bool attachmentsInFirstPartOnly = true;                     ///< Q6: the attachments of the document are kept only in the first part
    };

    /// One finished part, handed to the receiver
    struct PartResult
    {
        size_t partIndex = 0;                                       ///< Index of the part
        QByteArray data;                                            ///< The bytes of the file of the part
        PDFInteger pageCount = 0;                                   ///< Pages of the part
        PDFDocumentPageOperations::RemovalStatistics statistics;    ///< What the removal of the other pages has changed
        QStringList conformanceRemoved;                             ///< Standards no longer declared ("PDF/A", "PDF/UA")
        qint64 imagesRecompressed = 0;                              ///< Images, which the optimization has changed
        bool attachmentsRemoved = false;                            ///< The attachments of the document were left out (a part after the first)
    };

    /// Receives each part in the order of the parts, in the thread of split; false = stop
    /// (the split ends as stopped, the parts after it are not made)
    using Receiver = std::function<bool(PartResult part)>;

    /// Result of a split
    struct Result
    {
        bool isSuccess = false;         ///< Every part was made and received
        bool isCanceled = false;        ///< Canceled by the control
        bool isStopped = false;         ///< The receiver has stopped it
        QString errorMessage;           ///< Translated reason of a failure
        size_t receivedCount = 0;       ///< Parts handed to the receiver
    };

    /// Makes the parts (see the class)
    /// \param document The document (it must outlive the call)
    /// \param parts The parts, each of at least one valid page; at most MAXIMUM_PART_COUNT
    /// \param settings What the split does
    /// \param control Cancellation (optional), checked between the parts
    /// \param progress Progress (optional): a step per part
    /// \param receiver Receiver of the parts (required)
    static Result split(const PDFDocument* document, const std::vector<Part>& parts, const Settings& settings,
                        const PDFOperationControl* control, PDFProgressSink* progress, const Receiver& receiver);

    /// Most parts of one split (plan of step 3 of phase 2, chapter 6: "more than 10,000 files")
    static constexpr size_t MAXIMUM_PART_COUNT = 10000;
};

}   // namespace pdf

#endif // PDFDOCUMENTSPLITTER_H
