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

#ifndef PDFDOCUMENTTOOL_BITONAL_H
#define PDFDOCUMENTTOOL_BITONAL_H

#include "pdfbitonaldocumentcreator.h"
#include "pdfdocumentimagelist.h"
#include "pdfdocumenttool.h"

#include <optional>
#include <vector>

namespace pdf
{

/// The tool Convert to Black & White (optimize.bitonal, Figma 304:5102 ... 307:6564; plan of step 4
/// of phase 2, 4.6, K4.2 - K4.4, K4.13, owner answers Q4.7, Q4.12, Q4.14, R4-8, R4-9, R4-10 a,
/// R4-20, R4-21, R4-22; package P3) over PDFBitonalDocumentCreator. Images: the items of
/// PDFDocumentImageList with the options of a change (the content, not /Resources, B61; inline
/// images are kept, R4-8; the OCR text layers of PDF4QT bound again, R4-9). Pages: one image per
/// page over its media box, the layers merged by OCUsage::Export (R4-10 a), the rendering by
/// PDFRasterizerPool (the budget of 4 GiB, Q4.7). Cancellation in both modes (B58); the detection of
/// blank pages is a function of the core (B67).
///
/// Parameters: source (images | pages), method (automatic | threshold | adaptive | dither),
/// threshold (0 to 255), dpi (0 = estimated, else 24 to 600), compression (auto | flate | runlength
/// | ccitt-g4 | jbig2), items (ITEMS syntax below; never stored), replaceBlankPages (MCP: the
/// detection and the white fill in one run).
///
/// Syntax of "items": entries separated by ';', each "<target>=<mode>"; the target is a page
/// number from 1 ("12") or an image id of PDFDocumentImageList::Key::toString ("148 0"); the mode
/// is convert | convert-inverted | unchanged | fill-black | fill-white. Example:
/// "1=fill-white; 148 0=convert-inverted". A target without an entry is converted.
///
/// Result: ChangeDocument (Copy preselected; LostCopyOnly, Q4.14); a partial conversion is Success
/// with the facts of the items, which were not converted (R4-20: MCP gets no question).
///
/// The document of the result is written whole (it is smaller only without the old revisions),
/// its version and source hash are those of the input, the declarations of PDF/A and PDF/UA are
/// removed and said (14.11.5; a page of an image has no tags). Facts: "items" (117 converted · 1
/// filled white · 2 unchanged), "size" (24.8 MB → 1.9 MB (92 % smaller) · JBIG2), "notConverted"
/// (pages 17, 43, 44 (rendering errors): a page, whose image could not be created), "renderingIssues"
/// (owner 14. 11. 2026: a page, whose rendering reported errors, is converted - "pages 17, 43 – check
/// these pages"; also PDFDocumentToolResult::renderingIssues and the data "renderingIssuePages"),
/// "blankPages", "layers" (Pages of a document with layers), "time", "conformance".
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolBitonal : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolBitonal)

public:
    using ItemMode = PDFBitonalDocumentCreator::ItemMode;

    /// One entry of "items": a page (index from 0) or an image, and its mode
    class PDF4QTLIBCORESHARED_EXPORT ItemEntry
    {
    public:
        PDFInteger pageIndex = -1;          ///< The page (index from 0), -1 = an image
        PDFDocumentImageList::Key image;    ///< The image (valid when pageIndex is -1)
        ItemMode mode = ItemMode::Algorithm;

        bool isPage() const { return pageIndex >= 0; }
    };

    /// Parses the text of "items" (see the class): std::nullopt and the translated reason of
    /// the first wrong entry
    static std::optional<std::vector<ItemEntry>> parseItems(const QString& text, QString* errorMessage);

    /// The text of "items" of the entries (the panel writes the modes by it; an entry of the mode
    /// convert is written too - the text says only what is given)
    static QString createItemsText(const std::vector<ItemEntry>& entries);

    /// Identifier of a mode ("fill-white") and back (std::nullopt = an unknown text)
    static QString getModeId(ItemMode mode);
    static std::optional<ItemMode> getMode(const QString& modeId);

    /// The method and the compression of the values of the parameters
    static PDFImageConversion::ConversionMethod getConversionMethod(const QString& methodId);
    static PDFBitonalDocumentCreator::Compression getCompression(const QString& compressionId);

    /// The conversion with the color management of the rendering of the pages (execute gives
    /// the default settings: a tool has no manager of a view; PdfTool gives its own --cms
    /// options). The parameters are normalized and validated.
    PDFDocumentToolResult convert(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFCMSSettings& cmsSettings,
                                  const PDFOperationControl* control, PDFProgressSink* progress) const;

    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;

    static constexpr const char* TOOL_ID = "optimize.bitonal";

    /// Names of the parameters
    static constexpr const char* PARAMETER_SOURCE = "source";
    static constexpr const char* PARAMETER_METHOD = "method";
    static constexpr const char* PARAMETER_THRESHOLD = "threshold";
    static constexpr const char* PARAMETER_DPI = "dpi";
    static constexpr const char* PARAMETER_COMPRESSION = "compression";
    static constexpr const char* PARAMETER_ITEMS = "items";
    static constexpr const char* PARAMETER_REPLACE_BLANK_PAGES = "replaceBlankPages";

    /// Identifiers of Source (304:5102)
    static constexpr const char* SOURCE_IMAGES = "images";
    static constexpr const char* SOURCE_PAGES = "pages";

    /// Identifiers of Method (306:5353)
    static constexpr const char* METHOD_AUTOMATIC = "automatic";
    static constexpr const char* METHOD_THRESHOLD = "threshold";
    static constexpr const char* METHOD_ADAPTIVE = "adaptive";
    static constexpr const char* METHOD_DITHER = "dither";

    /// Identifiers of Compression (306:5353)
    static constexpr const char* COMPRESSION_AUTO = "auto";
    static constexpr const char* COMPRESSION_FLATE = "flate";
    static constexpr const char* COMPRESSION_RUN_LENGTH = "runlength";
    static constexpr const char* COMPRESSION_CCITT_G4 = "ccitt-g4";
    static constexpr const char* COMPRESSION_JBIG2 = "jbig2";

    /// Modes of an item (306:5615)
    static constexpr const char* MODE_CONVERT = "convert";
    static constexpr const char* MODE_CONVERT_INVERTED = "convert-inverted";
    static constexpr const char* MODE_UNCHANGED = "unchanged";
    static constexpr const char* MODE_FILL_BLACK = "fill-black";
    static constexpr const char* MODE_FILL_WHITE = "fill-white";

    /// Resolution of the pages (306:5353: 24 to 600 dpi, step 25; 0 = estimated from the images)
    static constexpr int MINIMUM_DPI = 24;
    static constexpr int MAXIMUM_DPI = 600;

    /// Longest text of items
    static constexpr int MAXIMUM_ITEMS_LENGTH = 4 * 1024 * 1024;

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_BITONAL_H
