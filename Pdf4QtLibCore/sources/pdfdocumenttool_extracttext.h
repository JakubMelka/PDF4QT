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

#ifndef PDFDOCUMENTTOOL_EXTRACTTEXT_H
#define PDFDOCUMENTTOOL_EXTRACTTEXT_H

#include "pdfdocumenttextexporter.h"
#include "pdfdocumenttextflow.h"
#include "pdfdocumenttool.h"

namespace pdf
{

/// The tool Extract Text (convert.extract-text, Figma 339:8112 …; plan of the first batch,
/// K12, K20): the text of the selected pages in the reading order of PDFDocumentTextFlowFactory,
/// written by PDFDocumentTextExporter as plain text, HTML or XML in UTF-8, Latin-1 or the
/// encoding of the system - one file of the result (PDFDocumentToolResultKind::Files), whose
/// name the result dialog or MCP asks for.
///
/// Result:
///   Success - one output file "<name>.txt / .html / .xml" with its type and charset; facts
///   "pages", "words", "size" of the result dialog 339:9601 and "encoding" when characters
///   could not be written; data "pageCount", "pagesWithText", "pagesWithoutText", "words",
///   "characters", "size", "order" (the order used), "format", "encoding", "lostCharacters";
///   NothingToDo - the selected pages have no text (never an empty file): facts "pages";
///   Stopped - canceled, no file.
///
/// The preview (not the result) is bounded: the pages with a text are found by a walk, which
/// stops at the first character of a page and decodes no image; the words, characters and the
/// size are measured on at most PREVIEW_SAMPLE_PAGES of them and scaled ("about 412,880 words").
/// Its data: "tagged", "pageCount", "pagesWithText", "pagesWithoutText", "words", "characters",
/// "size", "order", "isEstimate".
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolExtractText : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolExtractText)

public:
    static constexpr const char* TOOL_ID = "convert.extract-text";

    /// Names of the parameters
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_ORDER = "order";
    static constexpr const char* PARAMETER_INCLUDE_PAGE_NUMBERS = "includePageNumbers";
    static constexpr const char* PARAMETER_INCLUDE_TITLES = "includeStructureTitles";
    static constexpr const char* PARAMETER_INCLUDE_LANGUAGES = "includeLanguages";
    static constexpr const char* PARAMETER_INCLUDE_ALTERNATIVE_DESCRIPTIONS = "includeAlternativeDescriptions";
    static constexpr const char* PARAMETER_INCLUDE_EXPANDED_FORMS = "includeExpandedForms";
    static constexpr const char* PARAMETER_INCLUDE_ACTUAL_TEXT = "includeActualText";
    static constexpr const char* PARAMETER_INCLUDE_PHONEMES = "includePhonemes";
    static constexpr const char* PARAMETER_FORMAT = "format";
    static constexpr const char* PARAMETER_ENCODING = "encoding";

    /// Identifiers of the reading orders (339:9461)
    static constexpr const char* ORDER_AUTO = "auto";
    static constexpr const char* ORDER_LAYOUT = "layout";
    static constexpr const char* ORDER_CONTENT_STREAM = "content-stream";
    static constexpr const char* ORDER_STRUCTURE = "structure";

    /// Identifiers of the formats and encodings (339:9546)
    static constexpr const char* FORMAT_TEXT = "text";
    static constexpr const char* FORMAT_HTML = "html";
    static constexpr const char* FORMAT_XML = "xml";
    static constexpr const char* ENCODING_UTF8 = "utf-8";
    static constexpr const char* ENCODING_LATIN1 = "latin-1";
    static constexpr const char* ENCODING_SYSTEM = "system";

    /// Most pages, whose text the preview measures
    static constexpr int PREVIEW_SAMPLE_PAGES = 24;

    /// The included items (339:9492): the parameter, its option of the exporter in getOptions
    static std::vector<const char*> getIncludeParameters();

    /// What the exporter writes by the normalized parameters
    static PDFDocumentTextExporter::Options createOptions(const QVariantMap& parameters);

    /// Algorithm of the reading order of the parameters (auto is Auto)
    static PDFDocumentTextFlowFactory::Algorithm getAlgorithm(const QVariantMap& parameters);

    /// Identifier of an algorithm, which is not Auto ("layout", "content-stream", "structure")
    static QString getOrderId(PDFDocumentTextFlowFactory::Algorithm algorithm);

    /// Style of the formatter and encoding of the parameters
    static PDFOutputFormatter::Style getStyle(const QVariantMap& parameters);
    static PDFDocumentTextExporter::Encoding getEncoding(const QVariantMap& parameters);

    /// Extension of the file of the format ("txt", "html", "xml")
    static QString getExtension(const QVariantMap& parameters);

    /// Text flow of the pages by the parameters (the structure items of an export, setStructureExport)
    /// \param document Document
    /// \param pageIndices Pages
    /// \param parameters Normalized parameters
    /// \param control Cancellation (optional)
    /// \param progress Progress, a step per page (optional)
    static PDFDocumentTextFlow createFlow(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress);

    /// Writes and encodes the text of a flow by the parameters
    /// \param flow Text flow
    /// \param parameters Normalized parameters
    /// \param documentTitle Title of an HTML file
    static PDFDocumentTextExporter::EncodedText exportText(const PDFDocumentTextFlow& flow, const QVariantMap& parameters, const QString& documentTitle);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual bool hasPreview() const override { return true; }
    virtual PDFDocumentToolResult computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

protected:
    /// Definitions of the parameters of the tool (the preview of a page adds its own)
    PDFDocumentToolParameterDefinitions createParameters() const;

    /// Pages of the selection of the parameters, the reason of an error in \p errorMessage
    static std::vector<PDFInteger> getSelectedPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage);
};

/// The preview of the text of one page of Extract Text (the pane EXTRACTED TEXT of 339:8112):
/// the tool with the parameter "previewPage". It is no tool of the registry, the GUI runs it
/// in its own runner, so the preview of a page has its own cache and the estimate of the
/// whole document is not computed again for a page. Its preview: data "pageIndex", "text"
/// (the output of the format, at most PREVIEW_TEXT_LIMIT characters), "isTruncated",
/// "hasText", "tagged"; the selection of pages does not limit it.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolExtractTextPage : public PDFDocumentToolExtractText
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolExtractTextPage)

public:
    static constexpr const char* PARAMETER_PREVIEW_PAGE = "previewPage";

    /// Longest text of the preview of a page (characters)
    static constexpr int PREVIEW_TEXT_LIMIT = 20000;

    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual PDFDocumentToolResult computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_EXTRACTTEXT_H
