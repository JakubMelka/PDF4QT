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

#ifndef PDFPAGE_H
#define PDFPAGE_H

#include "pdfobject.h"

#include <QRectF>

#include <set>
#include <optional>

namespace pdf
{
class PDFDocument;

/// This enum represents number of degree, which should be page rotated CLOCKWISE,
/// when being displayed or printed.
enum class PageRotation
{
    None,
    Rotate90,
    Rotate180,
    Rotate270
};

/// This class represents attributes, which are inheritable. Also allows merging from
/// parents.
class PDFPageInheritableAttributes
{
public:
    explicit inline PDFPageInheritableAttributes() = default;

    /// Parses inheritable attributes from the page tree node
    /// \param templateAttributes Template attributes
    /// \param dictionary Dictionary, from which the data will be read
    /// \param document Document owning this data
    static PDFPageInheritableAttributes parse(const PDFPageInheritableAttributes& templateAttributes, const PDFObject& dictionary, const PDFDocument* document);

    const QRectF& getMediaBox() const { return m_mediaBox; }
    const QRectF& getCropBox() const { return m_cropBox; }
    PageRotation getPageRotation() const;
    const PDFObject& getResources() const { return m_resources; }

private:
    QRectF m_mediaBox;
    QRectF m_cropBox;
    std::optional<PageRotation> m_pageRotation;
    PDFObject m_resources;
};

/// Object representing page in PDF document. Contains different page properties, such as
/// media box, crop box, rotation, etc. and also page content, resources.
class PDFPage
{
public:
    explicit PDFPage() = default;

    /// Parses the page tree. If error occurs, then exception is thrown.
    /// \param document Document owning this tree
    /// \param root Root object of page tree
    static std::vector<PDFPage> parse(const PDFDocument* document, const PDFObject& root);

    inline const QRectF& getMediaBox() const { return m_mediaBox; }
    inline const QRectF& getCropBox() const { return m_cropBox; }
    inline const QRectF& getBleedBox() const { return m_bleedBox; }
    inline const QRectF& getTrimBox() const { return m_trimBox; }
    inline const QRectF& getArtBox() const { return m_artBox; }
    inline PageRotation getPageRotation() const { return m_pageRotation; }
    inline const PDFObject& getResources() const { return m_resources; }
    inline const PDFObject& getContents() const { return m_contents; }

    QRectF getRectMM(const QRectF& rect) const;

    inline QRectF getMediaBoxMM() const { return getRectMM(m_mediaBox); }
    inline QRectF getCropBoxMM() const { return getRectMM(m_cropBox); }
    inline QRectF getBleedBoxMM() const { return getRectMM(m_bleedBox); }
    inline QRectF getTrimBoxMM() const { return getRectMM(m_trimBox); }
    inline QRectF getArtBoxMM() const { return getRectMM(m_artBox); }

    static QRectF getRotatedBox(const QRectF& rect, PageRotation rotation);

private:
    /// Parses the page tree (implementation). If error occurs, then exception is thrown.
    /// \param pages Page array. Pages are inserted into this array
    /// \param visitedReferences Visited references (to check cycles in page tree and avoid hangup)
    /// \param templateAttributes Template attributes (inheritable attributes defined in parent)
    /// \param root Root object of page tree
    /// \param document Document owning this tree
    static void parseImpl(std::vector<PDFPage>& pages,
                          std::set<PDFObjectReference>& visitedReferences,
                          const PDFPageInheritableAttributes& templateAttributes,
                          const PDFObject& root,
                          const PDFDocument* document);

    QRectF m_mediaBox;
    QRectF m_cropBox;
    QRectF m_bleedBox;
    QRectF m_trimBox;
    QRectF m_artBox;
    PageRotation m_pageRotation = PageRotation::None;
    PDFObject m_resources;
    PDFObject m_contents;
};

}   // namespace pdf

#endif // PDFPAGE_H
