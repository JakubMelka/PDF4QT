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

#ifndef PDFDOCUMENTTOOL_PAGES_H
#define PDFDOCUMENTTOOL_PAGES_H

#include "pdfdocumenttool.h"
#include "pdfdocumentpageoperations.h"

#include <map>
#include <optional>

namespace pdf
{

/// The tool Rotate Pages (pages.rotate, Figma 389:20177, 389:20910, 389:21590; plan of
/// the first batch, chapter 6): the pages are turned in the file. The parameters are
/// either the pages and the rotation (all of them turned the same way), or the plan of
/// more rotations in one undo step ("9,10,15:right; 20:half"), which the panels always send.
///
/// Result:
///   Success - the changed document; facts "rotated" ("3 pages · 9, 10, 15") and
///   "rotation" ("90° right", "Mixed" for more angles, K14); data "rotatedCount",
///   "rotatedPages" (numbers from 1), "rotation" (right, left, half, mixed);
///   NothingToDo - every page has the rotation already (a reset of an unrotated page);
///   Failed - an invalid plan or page selection (by validate).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolRotatePages : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolRotatePages)

public:
    static constexpr const char* TOOL_ID = "pages.rotate";

    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_ROTATION = "rotation";
    static constexpr const char* PARAMETER_PLAN = "plan";

    /// Identifiers of the rotations
    static constexpr const char* ROTATION_LEFT = "left";
    static constexpr const char* ROTATION_RIGHT = "right";
    static constexpr const char* ROTATION_HALF = "half";
    static constexpr const char* ROTATION_RESET = "reset";

    /// Longest plan
    static constexpr int MAXIMUM_PLAN_LENGTH = 65536;

    /// A turn of a page (relative to its rotation) or its reset to /Rotate 0
    struct Turn
    {
        PageRotation turn = PageRotation::None; ///< The turn (Rotate90 = right, Rotate270 = left, Rotate180 = half)
        bool isReset = false;                   ///< The page gets the rotation 0 (Q9)
    };

    /// Turn of an identifier of a rotation, std::nullopt for an unknown one
    static std::optional<Turn> getTurn(const QString& rotation);

    /// Identifier of a turn of a page (right, left, half), empty for None
    static QString getTurnName(PageRotation turn);

    /// Parses the plan of the rotations: groups "<pages>:<rotation>" separated by ";",
    /// the pages in the syntax of PDFDocumentToolPageSelection, each page at most once.
    /// \returns the turns of the pages (indices from 0); std::nullopt and the translated
    ///          reason in \p errorMessage for an invalid plan
    static std::optional<std::map<PDFInteger, Turn>> parsePlan(const QString& plan, const PDFDocument* document, QString* errorMessage);

    /// The plan text of turns of pages (indices from 0): groups by the rotation, the pages in
    /// ranges, a group never longer than PDFDocumentToolPageSelection::MAXIMUM_PAGES_LENGTH
    static QString createPlan(const std::map<PDFInteger, PageRotation>& turns);

    /// The plan text of turns and resets of pages (indices from 0): the groups right, left,
    /// half, reset in this order (parsePlan reads it back)
    static QString createPlan(const std::map<PDFInteger, Turn>& turns);

    /// The changes of the pages, which the parameters ask for (by the plan, else by the
    /// pages and the rotation); std::nullopt and the reason for invalid parameters
    static std::optional<std::vector<PDFDocumentPageOperations::PageRotationChange>> createChanges(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage);

    /// Text of a turn: "90° right", "90° left", "180°"
    static QString getTurnText(PageRotation turn);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// The pages and the plan follow their page objects; a plan, whose pages are all gone, is
    /// empty and the pages select none (an empty plan never turns every page)
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;
};

/// The tool Remove Pages (pages.remove, Figma 388:20614, 388:21466, 388:22315; plan of the
/// first batch, chapters 6 and 7): PDFDocumentPageOperations::removePages of the selected
/// pages. Every page is an error (a document keeps at least one page).
///
/// Result:
///   Success - the changed document; facts "removed" ("4 pages · 4–6, 20"), "document"
///   ("44 pages (was 48)"), "tags" when the structure tree was removed (owner answer Q8),
///   "signatures" when signature fields went with their pages, "conformance"; data
///   "removedCount", "removedPages", "pageCount", "pageCountBefore" and the counters of
///   PDFDocumentPageOperations::RemovalStatistics;
///   Failed - no page, every page, an invalid selection (by validate).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolRemovePages : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolRemovePages)

public:
    static constexpr const char* TOOL_ID = "pages.remove";

    static constexpr const char* PARAMETER_PAGES = "pages";

    /// Indices of the pages of the parameters (from 0), empty and the reason for invalid ones
    static std::vector<PDFInteger> getPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// The marked pages follow their page objects; a page, which is gone, is no longer marked
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_PAGES_H
