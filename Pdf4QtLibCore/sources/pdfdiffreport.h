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

#ifndef PDFDIFFREPORT_H
#define PDFDIFFREPORT_H

#include "pdfdiff.h"
#include "pdfdocument.h"

#include <QColor>
#include <QCoreApplication>
#include <QString>

#include <optional>
#include <vector>

namespace pdf
{

class PDFDocumentToolInputDocument;
class PDFOperationControl;

/// The pages shown by Compare and written into its report (the menu View, 492:19823; plan of step 7 of phase 2,
/// 4.9): both documents side by side, one of them, or the revised page over the original one
enum class PDFDiffReportView
{
    SideBySide,     ///< The pairs of the page sequence: the original page, then the revised page
    Original,       ///< The original document only
    Revised,        ///< The revised document only
    Overlay         ///< The pairs of the page sequence, the revised page over the original page
};

/// Colors of the kinds of the differences (the panel Colors, 492:19314; the defaults are the tokens diff/removed,
/// diff/added, diff/replaced, diff/moved of the theme - #FF3232, #7DFA00, #FF781E, #A855F7)
class PDF4QTLIBCORESHARED_EXPORT PDFDiffReportColors
{
public:
    QColor removed = QColor(0xFF, 0x32, 0x32);
    QColor added = QColor(0x7D, 0xFA, 0x00);
    QColor replaced = QColor(0xFF, 0x78, 0x1E);
    QColor moved = QColor(0xA8, 0x55, 0xF7);
};

/// One page of a combined document (createCombinedDocument): the page of the original and of the revised
/// document it shows (indices from 0, -1 = none)
class PDF4QTLIBCORESHARED_EXPORT PDFDiffCombinedPage
{
public:
    PDFInteger originalPage = -1;
    PDFInteger revisedPage = -1;
};

/// The document of the view of Compare (createCombinedDocument): never null on a success, the open documents
/// never change (it is a new document of copies of their pages)
class PDF4QTLIBCORESHARED_EXPORT PDFDiffCombinedDocument
{
public:
    PDFDocumentPointer document;                ///< The shown document (null = failure)
    std::vector<PDFDiffCombinedPage> pages;     ///< Per page of \p document its pages of the inputs
    QString errorMessage;                       ///< Translated reason of a failure (B162: a failed merge is an error, never an empty document)

    bool isSuccess() const { return !document.isNull(); }
};

/// What the report PDF is made of (the dialog of the report, 492:107143)
class PDF4QTLIBCORESHARED_EXPORT PDFDiffReportSettings
{
public:
    PDFDiffReportView view = PDFDiffReportView::SideBySide;    ///< The pages of the report
    PDFDiffReportColors colors;                                 ///< Colors of the kinds of the differences
    PDFReal opacity = 0.3;                                      ///< /CA of the annotations of the differences (plan 4.9)
    QString author;                                             ///< /T of the annotations (the author name of the application)
};

/// The documents of Compare built from the two inputs and a result of PDFDiff (plan of step 7 of phase 2, 4.9,
/// 9.3; the code of the old Diff, Pdf4QtDiff/utils.cpp and mainwindow.cpp, moved to the core and corrected - it is
/// not copied):
///  - createCombinedDocument: the document, which the view of the tab of Compare shows (PDFAppCompareView) - the
///    pages of the view in the order of the page sequence, merged by PDFDocumentMerger (the one merger of Merge);
///    a failure is an error (B162), never an empty document with a page sequence;
///  - createReport: the report PDF - the combined document of the view with an annotation /Polygon per rectangle
///    of a difference in the color of its kind, /CA of the settings, /Contents = the description of the difference
///    (PDF/UA), /T = the author. The report is a new document of the inputs and it is not encrypted: the rule of
///    Merge (PDFDocumentMerger::getPermission of each input, the verified owner password of the input accepted) is
///    checked here for every caller (B161 / B210; the old Diff wrote it without any check).
/// Reentrant; the inputs are immutable snapshots shared by the caller (a closed source tab never invalidates them,
/// plan 6.3). The cancellation is checked per page.
///
/// Package P5 implements it; the skeleton has the interface (stubs: an error).
class PDF4QTLIBCORESHARED_EXPORT PDFDiffReportBuilder
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDiffReportBuilder)

public:
    PDFDiffReportBuilder() = delete;

    /// Most annotations of a report (a polygon per rectangle of a difference; a hostile pair of
    /// documents with millions of differences would give a report of millions of objects): more is
    /// an error, which asks to show fewer differences
    static constexpr qint64 MAXIMUM_ANNOTATION_COUNT = 100000;

    /// The document of a view (see the class)
    /// \param view The view (Overlay: the pairs as SideBySide - the view of the tab blends their pictures)
    /// \param original The original document (the first input)
    /// \param revised The revised document (the second input)
    /// \param result The result of the comparison (its page sequence; the shown differences are drawn by the caller)
    /// \param control Cancellation (optional)
    static PDFDiffCombinedDocument createCombinedDocument(PDFDiffReportView view,
                                                          const PDFDocumentPointer& original,
                                                          const PDFDocumentPointer& revised,
                                                          const PDFDiffResult& result,
                                                          const PDFOperationControl* control);

    /// The report PDF (see the class)
    /// \param original The original input (its document, name and verified owner password)
    /// \param revised The revised input
    /// \param result The shown differences (filtered by the caller)
    /// \param settings The view, the colors, the opacity and the author
    /// \param control Cancellation (optional)
    /// \param errorMessage Translated reason of a failure (optional; also the refusal of the rule of Merge)
    /// \returns The report, std::nullopt on a failure, a refusal or a cancellation
    static std::optional<PDFDocument> createReport(const PDFDocumentToolInputDocument& original,
                                                   const PDFDocumentToolInputDocument& revised,
                                                   const PDFDiffResult& result,
                                                   const PDFDiffReportSettings& settings,
                                                   const PDFOperationControl* control,
                                                   QString* errorMessage);
};

}   // namespace pdf

#endif // PDFDIFFREPORT_H
