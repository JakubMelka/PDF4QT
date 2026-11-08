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

#ifndef PDFDOCUMENTMANIPULATOR_H
#define PDFDOCUMENTMANIPULATOR_H

#include "pdfdocument.h"
#include "pdfutils.h"

#include <QImage>
#include <QMarginsF>

#include <map>

namespace pdf
{

/// Document page assembler/manipulator. Can assemble document(s) pages
/// to a new document, where pages are inserted/removed/moved, or joined
/// from another documents, or blank pages/image pages inserted. Document
/// is also optimized.
///
/// The adapter of the old PageMaster over PDFDocumentMerger (plan of step 2 of phase 2,
/// 5.1): the API stays, the merge is the one of the new tool Merge - a page, which is not
/// taken, never reaches the result (B3), a clone copies only the page and its annotations
/// (B4), the catalogs are joined (B1, B2), the crop margins start at the crop box (B14),
/// a document without the permission of the merge is refused (R2, B13).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentManipulator
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentManipulator)

public:
    explicit PDFDocumentManipulator() = default;

    /// Selects outline creation mode, when multiple documents
    /// are merged into one. For single document manipulation,
    /// this has no meaning.
    enum class OutlineMode
    {
        NoOutline,
        Join,
        DocumentParts
    };

    struct AssembledPage
    {
        PDFInteger documentIndex = -1; ///< Source document index. If page is not from a document, value is -1.
        PDFInteger imageIndex = -1; ///< Source image index. If page is not from a image, value is -1.
        PDFInteger pageIndex = -1; ///< Source document page index. If page is not from a document, value is -1.
        QSizeF pageSize; ///< Unrotated page size
        QSizeF sourcePageSize; ///< Source page size before item-specific crop, in millimeters.
        QMarginsF cropMarginsMM; ///< Item-specific crop margins: left, top, right, bottom, in millimeters.
        PageRotation sourcePageRotation = PageRotation::None; ///< Original source page rotation before item-specific rotation.
        PageRotation pageRotation = PageRotation::None; ///< Page rotation

        constexpr bool isDocumentPage() const { return documentIndex != -1; }
        constexpr bool isImagePage() const { return imageIndex != -1; }
        constexpr bool isBlankPage() const { return documentIndex == -1 && imageIndex == -1; }
    };

    using AssembledPages = std::vector<AssembledPage>;

    /// Adds document with given index to available document list
    /// \param documentIndex Document index
    /// \param document Document
    void addDocument(int documentIndex, const PDFDocument* document) { m_documents[documentIndex] = document; }

    /// Adds image with given index to available image list
    /// \param imageIndex Image index
    /// \param image Image
    void addImage(int imageIndex, QImage image) { m_images[imageIndex] = std::move(image); }

    /// Assembles pages into a new document. Returns true, if a new document
    /// was assembled, otherwise error message is being returned. Assebmled
    /// document can be accessed trough a given getters.
    /// \param pages Pages
    /// \returns True or error message
    PDFOperationResult assemble(const AssembledPages& pages);

    /// Returns reference to an assembled document. This function should
    /// be called only, if method \p assemble returns true, otherwise
    /// undefined document can be returned.
    /// \returns Assembled document
    const PDFDocument& getAssembledDocument() const { return m_assembledDocument; }

    /// Returns rvalue reference to an assembled document. This function should
    /// be called only, if method \p assemble returns true, otherwise
    /// undefined document can be returned.
    /// \returns Assembled document
    PDFDocument&& takeAssembledDocument() { return std::move(m_assembledDocument); }

    static AssembledPages createAllDocumentPages(int documentIndex, const PDFDocument* document);

    static AssembledPage createDocumentPage(int documentIndex, int pageIndex, QSizeF pageSize, PageRotation pageRotation) { AssembledPage page{ documentIndex, -1, pageIndex, pageSize, pageSize, QMarginsF(), pageRotation, pageRotation}; return page; }
    static AssembledPage createImagePage(int imageIndex, QSizeF pageSize, PageRotation pageRotation) { AssembledPage page{ -1, imageIndex, -1, pageSize, pageSize, QMarginsF(), PageRotation::None, pageRotation}; return page; }
    static AssembledPage createBlankPage(QSizeF pageSize, PageRotation pageRotation) { AssembledPage page{ -1, -1, -1, pageSize, pageSize, QMarginsF(), PageRotation::None, pageRotation}; return page; }

    OutlineMode getOutlineMode() const;
    void setOutlineMode(OutlineMode outlineMode);

private:
    std::map<PDFInteger, const PDFDocument*> m_documents;
    std::map<PDFInteger, QImage> m_images;
    PDFDocument m_assembledDocument;
    OutlineMode m_outlineMode = OutlineMode::DocumentParts;
};

}   // namespace pdf

#endif // PDFDOCUMENTMANIPULATOR_H
