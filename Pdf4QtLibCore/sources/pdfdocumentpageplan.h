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

#ifndef PDFDOCUMENTPAGEPLAN_H
#define PDFDOCUMENTPAGEPLAN_H

#include "pdfglobal.h"
#include "pdfdocumentmerger.h"
#include "pdfimagepagefactory.h"

#include <QCoreApplication>
#include <QMarginsF>
#include <QSizeF>
#include <QString>

#include <optional>
#include <vector>

namespace pdf
{

/// The plan of the pages of a result (plan of step 3 of phase 2, 4.3, K44): the one place,
/// which says, which page of which input goes where - Merge (its syntax unchanged),
/// Interleave Pages, Images to PDF and Manage Pages (3b). A plan is a vector of items; the
/// merger makes the document of it (createMergerItems, PDFDocumentMerger::merge).
///
/// Text of a plan (parse / toText; the syntax of Merge, PDFDocumentToolMerge): items separated
/// by commas, each "<letters><page>[-<page>][@<turn>][+]" - the input by its letters (A, B,
/// ..., Z, AA, ...), its pages from 1 (a range in either direction, "A6-1" is reversed), the
/// turn clockwise relative to the rotation of the page (@90, @180, @270), "+" a clone (a
/// further occurrence of a page). A page used more than once needs "+" on every occurrence
/// after the first. The text carries the input, the page, the turn and the clone; the size of
/// a blank page, the layout of an image and the group are values of the tool (not in the text).
///
/// Every function is reentrant (only its arguments).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentPagePlan
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentPagePlan)

public:
    PDFDocumentPagePlan() = delete;

    /// Longest letters of an input (more than enough for 1000 inputs, no overflow)
    static constexpr int MAXIMUM_LETTERS = 6;

    /// Longest text of a plan (a plan of 50 000 pages sorted by their numbers has about 400 000 characters)
    static constexpr int MAXIMUM_TEXT_LENGTH = 4 * 1024 * 1024;

    /// One page of the result
    struct Item
    {
        int input = 0;                          ///< Index of the input (A = 0)
        PDFInteger pageIndex = 0;               ///< Page of the input, frame of an image (from 0); not used for a blank page
        PageRotation turn = PageRotation::None; ///< Turn clockwise relative to the rotation of the page (an image page: the merger sets /Rotate)
        bool isClone = false;                   ///< A further occurrence of the page (a copy of the page object and of its annotations); Manage Pages sets it by Clone, never by the position - the merger item gets it (PDFDocumentMerger::Item::isClone, arrange keeps the original by it, RB4 of step 3b)
        QSizeF pageSize;                        ///< A blank page: its size in points; an image page: the size of the stretched page (empty = by the resolution)
        std::optional<PDFImagePageLayout> imageLayout;  ///< An image page: the layout of the image (std::nullopt = stretched, Merge)
        int group = -1;                         ///< Group of the page (Manage Pages), -1 = none
        QMarginsF cropMargins;                  ///< A PDF page: margins in points cut from its visible area (Manage Pages Crop…, Q17 of step 3, RB10 b of step 3b; PDFDocumentMerger::Item::cropMargins); empty = none. Not in the text of a plan.

        bool operator==(const Item&) const = default;
    };

    /// Letters of the input of the index: A to Z, then AA, AB, ... (R20 of step 2); empty for a negative index
    static QString getInputLetters(int inputIndex);

    /// Index of the input of its letters (upper case A-Z), -1 for an invalid text
    static int getInputIndex(QStringView letters);

    /// Parses the text of a plan for inputs of the given page counts (see the class).
    /// std::nullopt and the translated reason for an invalid text, a page out of range, a
    /// page twice without "+", a text longer than MAXIMUM_TEXT_LENGTH, more than
    /// PDFDocumentMerger::MAXIMUM_PAGE_COUNT pages, no page.
    static std::optional<std::vector<Item>> parse(const QString& text, const std::vector<PDFInteger>& pageCounts, QString* errorMessage);

    /// Text of a plan, which parse reads back: consecutive pages of an input with the same turn
    /// and clone flag become a range ("A1-6"), a reversed run a reversed range
    static QString toText(const std::vector<Item>& items);

    /// Every page of every input in their order
    static std::vector<Item> createDefault(const std::vector<PDFInteger>& pageCounts);

    /// How the sides of Interleave Pages are joined (5.7, 385:13263, 385:14487)
    struct InterleaveSettings
    {
        bool isBackReversed = true;     ///< The back sides are read from the last page to the first (Back sides: Reversed)
        bool areExtrasKept = true;      ///< The sides without a pair go to the end (Extra pages: At the end), else they are left out
    };

    /// The pages of two scans joined (5.7): the front sides are the input 0, the back sides the
    /// input 1; front 1, back 1, front 2, back 2, ... where the back side i is the page i of the
    /// back input, or the page n - i + 1 when it is reversed. The sides of the longer input,
    /// which have no pair, follow in their order at the end (or are left out).
    static std::vector<Item> interleave(PDFInteger frontPageCount, PDFInteger backPageCount, const InterleaveSettings& settings);

    /// The pages in the reverse order (the clones marked again: the first occurrence of a page
    /// is the page, the further ones its clones)
    static std::vector<Item> reverse(std::vector<Item> items);

    /// The items of the merger of the plan (one conversion for every tool; the merger makes a
    /// further occurrence of a page a clone itself)
    static std::vector<PDFDocumentMerger::Item> createMergerItems(const std::vector<Item>& items);
};

}   // namespace pdf

#endif // PDFDOCUMENTPAGEPLAN_H
