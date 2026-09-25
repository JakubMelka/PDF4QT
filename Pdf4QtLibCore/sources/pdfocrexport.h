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

#ifndef PDFOCREXPORT_H
#define PDFOCREXPORT_H

#include "pdfocrproject.h"

#include <QByteArray>
#include <QTransform>

#include <vector>

namespace pdf
{
class PDFDocument;

/// Export of the OCR results into the structured formats with the geometry:
/// hOCR 1.2, ALTO 4.4 and the TSV of Tesseract.
///
/// The coordinates refer to the visible page (the crop box rotated by /Rotate,
/// scaled by /UserUnit), rendered at the resolution of the export: the origin
/// is in the top-left corner, the y axis grows downwards, the unit is a pixel.
/// At the same resolution it is the image produced by the rendering of the page
/// into an image, so the export matches the exported page images.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRStructuredExporter
{
public:
    enum class Format
    {
        Hocr,   ///< hOCR 1.2 (XHTML)
        Alto,   ///< ALTO 4.4 (XML)
        Tsv     ///< Tab separated values of Tesseract
    };

    struct Options
    {
        Format format = Format::Hocr;

        /// Resolution of the coordinates in DPI. Zero means the resolution of the
        /// recognition of the page (DefaultDpi, if the page does not know it).
        double dpi = 0.0;

        /// Export only reviewed (confirmed/modified) words
        bool onlyReviewed = false;

        /// Normalize the text to NFC
        bool normalizeNFC = false;

        /// Export the scores of the engine (words with an unknown score have none)
        bool includeConfidence = true;

        /// Name of the image of the page (hOCR "image", ALTO "fileName"), %1 is
        /// the physical page number. Empty = no image is referenced.
        QString imageFileNameTemplate;

        /// Title of the document (hOCR title)
        QString title;
    };

    /// Resolution used for the pages without the resolution of the recognition
    static constexpr double DefaultDpi = 300.0;

    /// Exports the pages into a single file. Pages without a result are skipped and
    /// reported (EXPORT-02). The document is needed for the page geometry, pages,
    /// which are not in the document, are skipped as well.
    static QByteArray exportPages(const PDFDocument* document,
                                  const std::vector<const PDFOCRPageResult*>& pages,
                                  const Options& options,
                                  PDFOCRTextExporter::Report* report);

    /// Returns the resolution of the export of the page (see Options::dpi)
    static double getExportDpi(const PDFOCRPageResult& page, const Options& options);

    /// Returns the transformation from the canonical page space to the pixels of
    /// the export and the size of the page in pixels. Returns false, if the page
    /// does not exist or has an invalid size.
    static bool getPageTransform(const PDFDocument* document, PDFInteger pageIndex, double dpi, QTransform* pageToExport, QSize* size);

    /// Converts the language code of the engine ("ces", "chi_sim", "ces@import")
    /// into a BCP 47 language tag ("cs", "zh-Hans"). Returns an empty string, if the
    /// code is not a language (a script model, for example "script/Latin").
    static QString toLanguageTag(const QString& engineLanguage);

    /// Returns the translated name of the format
    static QString getFormatName(Format format);

    /// Returns the default suffix of the file of the format (without the dot)
    static QString getFileSuffix(Format format);

    /// Returns the filter of the file dialog of the format
    static QString getFileFilter(Format format);

    /// Returns the name of the file of a page, when the pages are exported into
    /// separate files: the physical page number is appended to the base name
    /// ("book.xml" -> "book_p007.xml"), padded to the digits of the page count
    static QString getPageFileName(const QString& fileName, PDFInteger pageIndex, PDFInteger pageCount);

    /// Writes the data atomically into the file (QSaveFile)
    static bool writeFile(const QString& fileName, const QByteArray& data, QString* errorMessage);
};

}   // namespace pdf

#endif // PDFOCREXPORT_H
