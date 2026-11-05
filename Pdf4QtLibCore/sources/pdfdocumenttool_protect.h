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

#ifndef PDFDOCUMENTTOOL_PROTECT_H
#define PDFDOCUMENTTOOL_PROTECT_H

#include "pdfdocumenthiddendatascan.h"
#include "pdfdocumentlinkoperations.h"
#include "pdfdocumenttool.h"

#include <set>
#include <vector>

namespace pdf
{

/// The tool Remove Hidden Data (protect.sanitize, Figma 438:12588 …, plan of the first batch,
/// chapters 1.3, 4 and 7): the kinds of hidden data of PDFDocumentHiddenDataScan, each a Bool
/// parameter, removed by PDFDocumentSanitizer. It rewrites the whole file (LostCopyOnly): the
/// result is made by createWholeFileDocument, so no old revision and no dead container of the
/// organization of the file keeps what was removed.
///
/// Preview (not final): the scan of the document, data "scan" (Result::toJson) and "pageCount".
/// Result:
///   Success - the changed document "<name>_clean.pdf"; facts "removed", "size", "kept"; a step
///   per removed kind (438:14342); data "removedKinds", "keptKinds", "scan", the sizes and
///   signatures of finishWholeFileResult, "conformanceRemoved" with the fact "conformance";
///   NothingToDo - no chosen kind is in the document.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolSanitize : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolSanitize)

public:
    static constexpr const char* TOOL_ID = "protect.sanitize";

    using Kind = PDFDocumentHiddenDataScan::Kind;

    /// Presets of the menu Preset (438:12831; K18: Custom is a state of the label, no item)
    enum class Preset
    {
        Standard,           ///< Everything except the invisible text
        BeforeSending,      ///< Info, metadata, comments, index and thumbnails
        BeforePublishing,   ///< Also attachments – outline and page labels stay
        Everything,         ///< Every kind including the OCR text layer (and the signature fields)
        Custom              ///< The values match no preset
    };

    /// The values of the parameters of a preset (Custom: all off, Clear all)
    static QVariantMap getPresetValues(Preset preset);

    /// The preset of the values; only the kinds of \p comparedKinds are compared (a kind,
    /// which the document does not have, is shown unticked by every preset), the first match wins
    static Preset findPreset(const QVariantMap& values, const std::vector<Kind>& comparedKinds);

    /// Title and description of a kind (the rows of 438:12588), translated
    static QString getKindTitle(Kind kind);
    static QString getKindDescription(Kind kind);

    /// Text of a step of the result of a removed kind ("37 markup annotations removed", 438:14342)
    static QString getStepText(Kind kind, const PDFDocumentHiddenDataScan::KindResult& found);

    /// Icon of a kind (the tiles of 438:12588)
    static QString getKindIconName(Kind kind);

    /// The chosen kinds of the values
    static std::vector<Kind> getChosenKinds(const QVariantMap& values);

    /// Count of kinds as a text ("7 kinds of hidden data", "1 kind")
    static QString formatKindCount(int count, bool isOfHiddenData);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual bool hasPreview() const override { return true; }
    virtual PDFDocumentToolResult computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const override;
};

/// The tool Remove External Links (protect.remove-links, Figma 438:14751 …, plan of the first
/// batch, chapters 1.3 and 2.7): the links of PDFDocumentLinkOperations, which leave the
/// document; the parameter "exclude" keeps some of them. The page tree and the numbers of the
/// objects stay, a signed document is saved incrementally (IncrementalSave).
///
/// Preview (not final): data "links" (PDFDocumentLinkOperations::toJson), "internalLinks",
/// "truncated". Result: Success - the changed document "<name>_no_links.pdf" (flag Annotation),
/// facts "removed", "kept", "internal", data "removed", "kept", "internalLinks", "pages";
/// NothingToDo - no external link, or every one is kept; Failed - an id of "exclude" is no
/// external link of the document.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolRemoveLinks : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolRemoveLinks)

public:
    static constexpr const char* TOOL_ID = "protect.remove-links";
    static constexpr const char* PARAMETER_EXCLUDE = "exclude";

    /// The links of a preview
    static std::vector<PDFDocumentLinkOperations::ExternalLink> getPreviewLinks(const PDFDocumentToolResult& preview);

    /// Text of the ids of the kept links (the value of "exclude")
    static QString createExcludeText(const std::set<QString>& ids);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual bool hasPreview() const override { return true; }
    virtual PDFDocumentToolResult computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_PROTECT_H
