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

#ifndef PDFDOCUMENTSIZEBREAKDOWN_H
#define PDFDOCUMENTSIZEBREAKDOWN_H

#include "pdfdocumenttool.h"

#include <QJsonObject>

#include <vector>

namespace pdf
{

/// Where the size of a file is: the rows of What changes of the tool Compress (Figma
/// 291:994, 295:2182). The images, fonts and page content are the sizes of their
/// objects; Structure is the rest of the file, so the rows sum to the size of the file.
///
/// An object belongs to a row by what it is, wherever it is used:
///   Images - every image XObject, also a mask, wherever it is drawn;
///   Fonts - everything reachable from a font dictionary or a font descriptor (programs
///   of the fonts, encodings, CMaps, procedures of Type 3 fonts);
///   Page content - content streams of the pages and form XObjects.
/// PDFObjectClassifier is not used: it looks only into the resources of the page
/// itself (not inherited ones, not those of a form) and marks the font dictionaries
/// without their programs, so the fonts and the images of a real document would
/// be reported as Structure.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentSizeBreakdown
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentSizeBreakdown)

public:
    qint64 images = 0;          ///< Bytes of the images
    qint64 fonts = 0;           ///< Bytes of the fonts
    qint64 pageContent = 0;     ///< Bytes of the content of the pages
    qint64 structure = 0;       ///< The rest of the file
    qint64 imageCount = 0;      ///< Images, not counting the masks of other images

    /// Sum of the rows
    qint64 getTotal() const { return images + fonts + pageContent + structure; }

    /// Computes the breakdown of a document
    /// \param document Document, may be null (an empty breakdown)
    /// \param fileSize Size of its file, the rest of it is Structure. When it is not
    ///        positive, Structure is the size of the other objects.
    /// \param control Cancellation, checked per object (optional); a canceled
    ///        computation returns an incomplete breakdown
    static PDFDocumentSizeBreakdown create(const PDFDocument* document, qint64 fileSize, const PDFOperationControl* control = nullptr);

    /// Structured data of the breakdown ("images", "fonts", "pageContent", "structure", "imageCount")
    QJsonObject toJson() const;

    /// Breakdown of the structured data
    static PDFDocumentSizeBreakdown fromJson(const QJsonObject& object);

    /// Rows of What changes in their order: "Images (412)" "21.3 MB → 3.4 MB", "Fonts",
    /// "Page content", "Structure" (ids "images", "fonts", "pageContent", "structure")
    static std::vector<PDFDocumentToolFact> createChangeRows(const PDFDocumentSizeBreakdown& before, const PDFDocumentSizeBreakdown& after);
};

}   // namespace pdf

#endif // PDFDOCUMENTSIZEBREAKDOWN_H
