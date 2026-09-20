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

#ifndef PDFOCRTEXTLAYERWRITER_H
#define PDFOCRTEXTLAYERWRITER_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfocrmodel.h"

#include <QDateTime>

#include <optional>

namespace pdf
{
class PDFDocument;
class PDFDocumentBuilder;

/// Writer of the invisible text layer (chapter 9 of the OCR specification).
/// The text is written with standard text operators in rendering mode 3
/// using an embedded glyphless CID font with identity /ToUnicode mapping.
/// Every page with own layer carries private data in the /PieceInfo
/// dictionary of the page (PDF-09), from which the layer can be read again.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRTextLayerWriter
{
public:
    /// Prefix of the font resource key
    static constexpr const char* FONT_RESOURCE_PREFIX = "PDF4QT_OCR";

    /// Key of the private data in the /PieceInfo dictionary
    static constexpr const char* PIECE_INFO_KEY = "PDF4QT_OCR";

    /// Version of the layer format
    static constexpr int LAYER_VERSION = 1;

    /// Information about the own OCR layer of the page
    struct LayerInfo
    {
        bool isPresent = false;
        int version = 0;
        QString layerId;
        int generation = 0;
        QString engineId;
        QString engineVersion;
        QStringList modelIds;
        QString modelSetHash;
        QByteArray pageFingerprint;
        PDFObjectReference contentReference;
        PDFObjectReference fontReference;
        PDFObjectReference dataReference;
        QByteArray fontKey;
        bool hasReviewData = false;
        bool fingerprintMatches = false;
        int wordCount = 0;
        QDateTime created;
    };

    /// Returns reference of the content stream of the own layer (without any
    /// verification), or invalid reference, if page has no own layer metadata.
    static PDFObjectReference getOwnLayerContentReference(const PDFDocument* document, PDFInteger pageIndex);

    /// Reads the information about own OCR layer of the page (PDF-09).
    /// Validates the binding of the metadata to the actual page content.
    static LayerInfo readLayerInfo(const PDFDocument* document, PDFInteger pageIndex);

    /// Reads the own OCR layer of the page (PDF-10). If review data are not
    /// present, confidence of the words is unknown and words are unreviewed.
    static std::optional<PDFOCRPageResult> readLayer(const PDFDocument* document, PDFInteger pageIndex, LayerInfo* info = nullptr);

    struct Options
    {
        /// Store detailed review data (original text, scores, states) in the document (PDF-10)
        bool keepReviewData = false;

        /// Write only reviewed words (confirmed or modified)
        bool onlyReviewed = false;

        /// Compress the streams
        bool compress = true;

        /// Mark the text layer as an artifact (used for tagged documents, so the existing
        /// structure tree and its bindings are preserved, PDF-14)
        bool markAsArtifact = false;
    };

    struct PageRequest
    {
        PDFInteger pageIndex = -1;
        PDFOCRPageResult result;
        QString layerId;
    };

    struct Report
    {
        std::vector<PDFInteger> writtenPages;
        std::vector<PDFInteger> unchangedPages;
        std::vector<PDFInteger> skippedPages;
        QStringList messages;
        PDFOCRError error;
        int writtenWords = 0;

        bool isModified() const { return !writtenPages.empty(); }
    };

    /// Applies the results into the document (PDF-03, PDF-04, PDF-05, PDF-11).
    /// Existing own layers of the pages are replaced. Pages with identical layer
    /// are left unchanged (idempotent operation). On error, the builder must be
    /// discarded, because the document could be partially modified.
    /// \param builder Document builder
    /// \param originalDocument Original document (for fingerprints and layer detection)
    /// \param pages Page requests
    /// \param options Options
    static Report apply(PDFDocumentBuilder* builder,
                        const PDFDocument* originalDocument,
                        const std::vector<PageRequest>& pages,
                        const Options& options);

    /// Removes the own OCR layer of the page including the private data (PDF-11).
    /// Returns true, if the layer was removed.
    static bool removeLayer(PDFDocumentBuilder* builder, const PDFDocument* originalDocument, PDFInteger pageIndex);

    /// Creates the content stream of the text layer (PDF-04, PDF-07, PDF-08)
    /// \param result Page result
    /// \param fontKey Font resource key
    /// \param onlyReviewed Write only reviewed words
    /// \param writtenWords Number of written words
    /// \param warnings Warnings
    static QByteArray createContentStream(const PDFOCRPageResult& result,
                                          const QByteArray& fontKey,
                                          bool onlyReviewed,
                                          int* writtenWords,
                                          QStringList* warnings,
                                          bool markAsArtifact = false);

    /// Removes the PDF/A and PDF/UA conformance declaration from the XMP metadata
    /// of the document (PDF-15). Returns true, if a declaration was removed.
    static bool removeConformanceDeclaration(PDFDocumentBuilder* builder, const PDFDocument* document);

    /// Creates the glyphless font in the document (PDF-07)
    static PDFObjectReference createGlyphlessFont(PDFDocumentBuilder* builder, bool compress);

    /// Returns the embedded glyphless font program (TrueType)
    static QByteArray getGlyphlessFontProgram();

    /// Returns the /ToUnicode CMap of the glyphless font
    static QByteArray getToUnicodeCMap();

    /// Computes the horizontal scaling (in percent) of the word text placed
    /// into the geometry of the word (EDIT-04).
    static double computeHorizontalScaling(const PDFOCRWord& word);

    /// Returns true, if horizontal scaling is extreme
    static bool isExtremeScaling(double horizontalScaling);

    /// Encodes the text as UTF-16BE hex string (CIDs of the glyphless font)
    static QByteArray encodeText(const QString& text, int* codeUnitCount);

    /// Formats the number for the content stream (no exponent, limited precision)
    static QByteArray formatNumber(double value);

private:
    static PDFObjectReference createStream(PDFDocumentBuilder* builder, PDFDictionary dictionary, const QByteArray& data, bool compress);
    static QByteArray serializeLayerData(const PDFOCRPageResult& result, const QString& layerId, bool keepReviewData);
    static bool deserializeLayerData(const QByteArray& data, PDFOCRPageResult& result, bool* hasReviewData);
};

}   // namespace pdf

#endif // PDFOCRTEXTLAYERWRITER_H
