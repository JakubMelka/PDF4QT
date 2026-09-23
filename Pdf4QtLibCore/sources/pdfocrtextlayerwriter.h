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
        PDFObjectReference isolationBeginReference; ///< Content stream "q" placed before the foreign content
        PDFObjectReference isolationEndReference;   ///< Content stream "Q" placed after the foreign content
        QByteArray fontKey;
        bool hasReviewData = false;
        bool fingerprintMatches = false;
        int wordCount = 0;
        QDateTime created;

        /// Metadata of the layer are an untrusted input. Objects are removed from the
        /// page (and from the document) only if they are verified to be objects of
        /// the own layer, otherwise a forged metadata could remove foreign content.
        bool isContentOwn = false;      ///< Content stream is part of the page and looks like the own text layer
        bool isDataOwn = false;         ///< Data stream contains the data of the own layer
        bool isFontOwn = false;         ///< Font is the glyphless font of the own layer
        bool isIsolationOwn = false;    ///< Isolation streams are part of the page and contain only q / Q
    };

    /// Returns reference of the content stream of the own layer (without any
    /// verification), or invalid reference, if page has no own layer metadata.
    static PDFObjectReference getOwnLayerContentReference(const PDFDocument* document, PDFInteger pageIndex);

    /// Returns references of all content streams, which belong to the own layer
    /// (text layer and the streams isolating the graphic state of the foreign
    /// content), without any verification. These streams are not part of the
    /// page fingerprint.
    static std::vector<PDFObjectReference> getOwnLayerContentReferences(const PDFDocument* document, PDFInteger pageIndex);

    /// Returns references of the content streams of the page in the order of the
    /// page dictionary (single stream, or array of streams).
    static std::vector<PDFObjectReference> getPageContentReferences(const PDFDocument* document, PDFInteger pageIndex);

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
        std::vector<PDFInteger> removedPages;   ///< Pages without text to write, whose obsolete own layer was removed
        QStringList messages;
        PDFOCRError error;
        int writtenWords = 0;

        bool isModified() const { return !writtenPages.empty() || !removedPages.empty(); }
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
    /// of the document (PDF-15). Returns true, if the metadata are without the
    /// declaration afterwards (also when there was nothing to remove); false, if the
    /// declaration could not be removed and the copy must not be written.
    static bool removeConformanceDeclaration(PDFDocumentBuilder* builder, const PDFDocument* document);

    /// Balance of the operators of a content stream (PDF-05)
    struct ContentBalance
    {
        int graphicStateDepth = 0;      ///< Unclosed "q" operators (negative = more "Q" than "q")
        int textObjectDepth = 0;        ///< Unclosed "BT" operators
        int markedContentDepth = 0;     ///< Unclosed "BMC" / "BDC" operators
        bool hasError = false;          ///< Lexical error, or "Q" / "ET" / "EMC" without the opening operator

        bool isBalanced() const { return graphicStateDepth == 0 && textObjectDepth == 0 && markedContentDepth == 0 && !hasError; }
    };

    /// Computes the balance of the nesting operators of the (decoded) content
    static ContentBalance computeContentBalance(const QByteArray& content);

    /// Returns true, if the content consists only of the operators of the invisible
    /// text layer (graphic state, text object, text state, text showing, marked
    /// content) with the text rendering mode 3, and is balanced. Anything else
    /// (paths, images, other rendering modes) makes the stream a foreign content.
    static bool isInvisibleTextStream(const QByteArray& content);

    /// Finds the PDF/A and PDF/UA conformance declarations in the XMP metadata (PDF-15).
    /// The properties are identified by their namespace, not by the prefix.
    /// \param metadata XMP packet
    /// \param declarations Human readable names of the found declarations
    /// \param withoutDeclarations The packet with the declarations removed (optional)
    static bool findConformanceDeclarations(const QByteArray& metadata, QStringList* declarations, QByteArray* withoutDeclarations);

    /// Validates the nesting of the whole content of the page by the parser (PDF-05):
    /// q / Q, BT / ET and marked content must be balanced over all content streams.
    /// Returns true, if the content is valid; the error message describes the problem.
    static bool validatePageContent(const PDFDocument* document, PDFInteger pageIndex, QString* errorMessage);

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
