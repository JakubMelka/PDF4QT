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

#ifndef PDFDOCUMENTHIDDENDATASCAN_H
#define PDFDOCUMENTHIDDENDATASCAN_H

#include "pdfdocument.h"
#include "pdfdocumentsanitizer.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QString>

#include <array>
#include <vector>

namespace pdf
{

class PDFOperationControl;
class PDFProgress;

/// What a document has of the hidden data, which Remove Hidden Data removes (Figma
/// 438:12588: the rows of the form, the card Scan; plan of the first batch, chapter 7). The
/// scan only reads: it never changes the document, it uses the same rules as
/// PDFDocumentSanitizer (isCommentAnnotation, isSignatureField, removeInvisibleText), so a
/// kind found here is the kind the sanitizer removes. It is bounded on a hostile file: the
/// walk over the objects visits each object once by an own stack (no recursion), the
/// annotations and the decoded content streams have their limits, a limit reached makes the
/// count of its kind a lower bound (isComplete false). It may run in a worker thread.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentHiddenDataScan
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentHiddenDataScan)

public:
    PDFDocumentHiddenDataScan() = delete;

    /// Kinds of hidden data in the order of the form (438:12588) and of the parameters of the tool
    enum class Kind
    {
        DocumentInfo,
        Metadata,
        Comments,
        Attachments,
        Outline,
        PageLabels,
        SearchIndex,
        Thumbnails,
        InvisibleText,
        SignatureFields     ///< The tenth kind (owner answer Q10)
    };

    static constexpr size_t KIND_COUNT = 10;

    /// Most bytes of the decoded content streams looked at for the invisible text
    static constexpr qint64 MAXIMUM_DECODED_CONTENT = qint64(1024) * 1024 * 1024;

    /// Most annotations looked at
    static constexpr qint64 MAXIMUM_ANNOTATIONS = 1000000;

    /// Deepest nesting of forms looked at for the invisible text (MAXIMUM_FORM_NESTING of the content processor)
    static constexpr int MAXIMUM_FORM_DEPTH = 32;

    /// What the scan has found of one kind
    struct KindResult
    {
        bool isPresent = false;     ///< The document has the kind
        qint64 count = 0;           ///< Items of the kind (entries, streams, annotations, files, items, pages, fields)
        qint64 bytes = 0;           ///< Bytes of the files of the attachments (their streams in the file)
        bool isComplete = true;     ///< false: a limit of the scan was reached, the count is a lower bound
    };

    /// Result of a scan
    struct PDF4QTLIBCORESHARED_EXPORT Result
    {
        std::array<KindResult, KIND_COUNT> kinds;
        QString pageLabelsText;     ///< The ranges of the page labels ("i–xii, 1–1,580")
        bool isCanceled = false;    ///< The scan was canceled: the result is not to be used

        const KindResult& get(Kind kind) const { return kinds[size_t(kind)]; }
        KindResult& get(Kind kind) { return kinds[size_t(kind)]; }

        /// Number of the kinds the document has
        int getFoundCount() const;

        /// Data of a preview of the tool and back
        QJsonObject toJson() const;
        static Result fromJson(const QJsonObject& object);
    };

    /// Scans the document
    /// \param document The document
    /// \param includeInvisibleText Look at the content streams for the invisible text (the
    ///        decoding of the content is the longest part of the scan)
    /// \param control Cancellation (optional)
    /// \param progress Progress, a step per page (optional)
    static Result scan(const PDFDocument* document, bool includeInvisibleText, const PDFOperationControl* control, PDFProgress* progress);

    /// All kinds in their order
    static std::vector<Kind> getKinds();

    /// The flag of the sanitizer of the kind
    static PDFDocumentSanitizer::SanitizationFlag getFlag(Kind kind);

    /// Name of the Bool parameter of the tool and of the key in JSON ("documentInfo")
    static QString getName(Kind kind);
};

}   // namespace pdf

#endif // PDFDOCUMENTHIDDENDATASCAN_H
