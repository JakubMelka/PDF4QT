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

#ifndef PDFDOCUMENTTOOL_SIGNBYHAND_H
#define PDFDOCUMENTTOOL_SIGNBYHAND_H

#include "pdfdocumenttool.h"

namespace pdf
{

/// Sign by Hand (protect.sign-by-hand; plan of step 6 of phase 2, 4.2, 5.5, 5.6; package P2 implements
/// the run). The drawing goes into the content of the pages (PDFPageDrawing::appendToPages, a stream after
/// the content of each page in q … Q, the old Sign Electronically) - no annotation, no /AcroForm, no
/// signature: "Not a digital ID signature". A change of the pages (the catalog isPageChange): a certified
/// document gets only a copy without its certification (PDFDocMDPPermissions::isCertificationRemovedBy);
/// a signed one an incremental update (the catalog impact IncrementalSave). Permission: bit 4.
///
/// Parameters (the names are PARAMETER_*):
///   - drawing (Value PDFPageDrawing; the panels give it, sensitive - R6-19): the elements of the scene of
///     PDFAppToolDrawingPart;
///   - elements (String, the JSON of PDFPageDrawing::toJson; MCP gives it, sensitive - R6-19): read by
///     PDFPageDrawing::fromJson strictly (no SVG image, no image - Q6.7, R6-20; the limits of 7.2);
///     checkGivenParameters refuses an action, which gives none or both.
/// One of them gives the drawing of the run; an empty drawing is an error (the confirmation is Disabled).
///
/// Result: the changed document (ChangeDocument, Keep preselected), the facts FACT_* (435:10812) and the
/// data DATA_*. A document declaring PDF/A or PDF/UA keeps the declaration and says so (the rule Q18 of
/// step 3; "The drawing is not tagged").
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolSignByHand : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolSignByHand)

public:
    static constexpr const char* TOOL_ID = "protect.sign-by-hand";

    /// Names of the parameters
    static constexpr const char* PARAMETER_DRAWING = "drawing";
    static constexpr const char* PARAMETER_ELEMENTS = "elements";

    /// Longest JSON of the parameter elements: the longest structured String of MCP
    /// (PDFDocumentToolParameterDefinition::setStructuredData). The drawing is bounded by its counts
    /// (PDFPageDrawing: elements, points of the curves, characters of a text - plan 7.2); the length only
    /// has to fit them: PDFPageDrawing::MAXIMUM_POINT_COUNT points written by toJson with full precision
    /// (about 40 characters a point) fit (ASan D1 of step 6)
    static constexpr int MAXIMUM_ELEMENTS_LENGTH = PDFDocumentToolParameterDefinition::MAXIMUM_DATA_TEXT_LENGTH;

    /// Names of the data of the result (MCP)
    static constexpr const char* DATA_ELEMENT_COUNT = "elementCount";  ///< Elements written
    static constexpr const char* DATA_PAGES = "pages";                  ///< The pages with a drawing (numbers from 1)

    /// Names of the facts of the result dialog (435:10812)
    static constexpr const char* FACT_DRAWN = "drawn";                  ///< Elements: "4 on pages 3 and 308", "1 on page 308", "6 on pages 1–3, 8"
    static constexpr const char* FACT_KIND = "kind";                    ///< Kind: "Drawing – not a digital ID signature"
    static constexpr const char* FACT_CONFORMANCE = "conformance";      ///< "The drawing is not tagged" (PDF/UA), the declarations kept

    PDFDocumentToolSignByHand() = default;

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_SIGNBYHAND_H
