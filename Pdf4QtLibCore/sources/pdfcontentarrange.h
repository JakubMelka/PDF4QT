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

#ifndef PDFCONTENTARRANGE_H
#define PDFCONTENTARRANGE_H

#include "pdfcontentcommands.h"
#include "pdfglobal.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <optional>
#include <vector>

namespace pdf
{

// Edit Content, stage E1, package P13 (the core of the card Arrange). Plan: zadani_jadro.md 4.13
// (PDFContentArrange: the 16 operations of the old PDFPageContentElementManipulator over
// rectangles, aligned and matched to the key object, centered on the page, distributed by the old
// rules; one undo step), 4.5 (commands and undo); zadani.md 12.2 QE.6 (until stage E3 a path is
// resized by a matrix too). The GUI gives the boxes and the key object as plain values
// (PDFAppEditContentGeometryHelper of the application): nothing here knows a GUI.
//
// Threads and ownership: the functions are reentrant and keep no state. A command is a value of
// the thread of its session, as every command (PDFContentCommand).

/// The geometry of the card Arrange (zadani_jadro.md 4.13) and its commands.
///
/// Space and edges: a box is a rectangle of one space with the y axis up, as the default space of
/// a page (ISO 32000-2, 8.3.2.3): its left edge is QRectF::left(), its right edge right(), its
/// lower edge QRectF::top() (the smaller y) and its upper edge QRectF::bottom() (the larger y). A
/// box with a negative width or height is normalized first (QRectF::normalized). A box, which is
/// null, empty in both dimensions or not finite, takes part in no operation: its matrix is the
/// identity and it does not count to the union.
///
/// The operations (the result is one matrix per box, a move or a change of the size in the space
/// of the boxes; the identity for a box, which does not change):
///  - Align (the key object is the reference, zadani_jadro.md 4.13; the old code aligned to the
///    union of all boxes): AlignLeft moves every box in x so that its left edge is the left edge of
///    the key box; AlignHorizontalCenter by the centers in x; AlignRight by the right edges;
///    AlignTop moves in y by the upper edges, AlignVerticalCenter by the centers in y, AlignBottom by
///    the lower edges. The key box itself does not move. Needs at least two boxes.
///  - Center on the page: the union of the boxes moves as a whole (every box by the same offset), so
///    that its center is the center of the page box in x (CenterHorizontallyOnPage), in y
///    (CenterVerticallyOnPage) or in both (CenterOnPage). One box is enough.
///  - Distribute (the old rules of PDFPageContentElementManipulator, the reference is the union U
///    of the boxes; needs at least two boxes):
///     - DistributeInRow: the boxes sorted by their left edges (a stable sort: equal edges keep the
///       order of the input) are put side by side from the left edge of U, each moved in x only:
///       the first box starts at U.left, the next one at the right edge of the previous one placed.
///     - DistributeInColumn: the boxes sorted by their upper edges, the highest first (stable), are
///       put one below the other from the upper edge of U, each moved in y only: the upper edge of
///       the first box is the upper edge of U, the upper edge of the next one is the lower edge of
///       the previous one placed.
///     - DistributeAsForm: two columns of labels and fields. A box is in the left column, when the
///       distance of its left edge from U.left is smaller than the distance of its right edge from
///       U.right, otherwise in the right one. Pairs (left, right) are made greedily by the largest
///       vertical overlap (the length of the intersection of their y intervals; only an overlap
///       larger than zero pairs): among all the pairs of the boxes not paired yet the one with the
///       largest overlap is taken, a tie is decided by the smaller index of the left box in the
///       input and then by the smaller index of the right box. The boxes left without a pair are
///       rows of their own (first the left ones, then the right ones, each in the order of the
///       input). The rows are sorted by the average of the centers in y of their boxes, the highest
///       first (stable). The rows are placed from the upper edge of U down: the left box of a row
///       gets its left edge at U.left, the right box its left edge at U.right - W, where W is the
///       largest width of a box in the right column; both get their upper edges at the upper edge of
///       the row; the next row starts at the upper edge of the row minus the larger height of its
///       boxes. Bound: at most MAXIMUM_FORM_BOXES boxes (the pairing compares every left box with
///       every right one); more are refused with a text.
///     - DistributeAsGrid: rows and columns are found, then every box is put into its cell. Rows:
///       the boxes sorted by their upper edges (ascending, stable); while boxes remain, the box with
///       the highest upper edge of the remaining ones (the last of the sorted remaining ones) starts
///       a row and takes every remaining box, whose y interval overlaps its own by more than zero;
///       the height of the row is the largest height of its boxes; the rows are numbered from 0 from
///       the top. Columns the same way in x: sorted by their left edges descending (stable), the box
///       with the smallest left edge of the remaining ones starts a column and takes every remaining
///       box, whose x interval overlaps its own by more than zero; the width of the column is the
///       largest width of its boxes; the columns are numbered from 0 from the left. A box of the row
///       r and the column c gets its left edge at U.left + the sum of the widths of the columns
///       before c, and its upper edge at the upper edge of U - the sum of the heights of the rows
///       before r. The cost is O(n log n) (the boxes, which overlap the starting box of a row, are a
///       run of the sorted remaining ones; the same for the columns).
///  - Match size (to the key box; needs at least two boxes): MatchWidth scales every other box in x
///    to the width of the key box, MatchHeight in y to its height, MatchSize both. The upper left
///    corner of a box stays where it is (the left edge and the upper edge). A dimension of a box,
///    which is zero (a horizontal or vertical line), or of the key box, which is zero, is not
///    scaled (a matrix with a zero scale would be singular).
///
/// Every number of the result is finite; an operation, whose result would not be (a box of huge
/// coordinates), is refused with a text.
class PDF4QTLIBCORESHARED_EXPORT PDFContentArrange
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFContentArrange)

public:
    PDFContentArrange() = delete;

    enum class Operation
    {
        AlignLeft,
        AlignHorizontalCenter,
        AlignRight,
        AlignTop,
        AlignVerticalCenter,
        AlignBottom,
        CenterHorizontallyOnPage,
        CenterVerticallyOnPage,
        CenterOnPage,
        DistributeInRow,
        DistributeInColumn,
        DistributeAsForm,
        DistributeAsGrid,
        MatchWidth,
        MatchHeight,
        MatchSize
    };

    /// The JSON types of the commands (zadani_jadro.md 4.5: the types of a script of MCP); the
    /// field "operation" names the operation (getOperationName)
    static constexpr const char* TYPE_ALIGN = "align";              ///< Align and center on the page
    static constexpr const char* TYPE_DISTRIBUTE = "distribute";
    static constexpr const char* TYPE_MATCH_SIZE = "match_size";

    /// The key of the JSON of the operation and of the matrices (one per identifier of "ids", the
    /// matrix of the default space of the container of the nodes, as "matrix" of a transform)
    static constexpr const char* KEY_OPERATION = "operation";
    static constexpr const char* KEY_MATRICES = "matrices";

    /// The most boxes of DistributeAsForm (the pairing is quadratic: 2000 boxes are 10^6
    /// comparisons at most)
    static constexpr size_t MAXIMUM_FORM_BOXES = 2000;

    /// The JSON type of the command of the operation (TYPE_ALIGN, TYPE_DISTRIBUTE, TYPE_MATCH_SIZE)
    static QByteArray getType(Operation operation);

    /// The name of the operation in the JSON ("left", "horizontal-center", "right", "top",
    /// "vertical-center", "bottom", "page-horizontal-center", "page-vertical-center",
    /// "page-center", "row", "column", "form", "grid", "width", "height", "size")
    static QByteArray getOperationName(Operation operation);

    /// The operation of the type and the name; no value for a name, which is not one of the type
    static std::optional<Operation> getOperation(const QByteArray& type, const QByteArray& name);

    /// The least number of the boxes the operation needs (1 for the center on the page, 2 else)
    static size_t getMinimumBoxCount(Operation operation);

    /// Computes the matrices of the operation (see the class).
    /// \param operation The operation
    /// \param boxes The boxes of the objects in one space with the y axis up
    /// \param keyIndex The index of the key box (Align, Match size); ignored by the others
    /// \param pageBox The box of the page in the same space (the center on the page); ignored by the others
    /// \param errorText Translated reason of a refusal
    /// \returns One matrix per box (in the space of the boxes; the identity for a box, which does not
    ///          change), or no value: fewer boxes than getMinimumBoxCount, a key index out of range
    ///          or of a box, which takes part in no operation (Align, Match size), a page box, which
    ///          is empty or not finite (the center on the page), more boxes than MAXIMUM_FORM_BOXES
    ///          (DistributeAsForm), a result, which is not finite
    static std::optional<std::vector<QTransform>> computeMatrices(Operation operation,
                                                                  const std::vector<QRectF>& boxes,
                                                                  size_t keyIndex,
                                                                  const QRectF& pageBox,
                                                                  QString* errorText);

    /// Creates the command of the operation over the nodes: one undo step, which transforms each
    /// node by its own matrix (as PDFContentCommandFactory::createTransform does for one matrix: R-1,
    /// R-5, R-6; a Group or a MarkedContent gives its matrix to its leaves; a node below another
    /// node of the command moves with it only). The boxes and the page box are in the default space
    /// of the page; \p containerToPage maps the default space of the container of the nodes to it
    /// (the identity for the page itself; PDFAppEditContentGeometryHelper::getContainerToPageMatrix):
    /// the matrix T of a box becomes C x T x C^-1 in the container (row vectors, C = containerToPage).
    /// \param nodes The nodes of one container, distinct
    /// \param boxes Their boxes (the same count)
    /// \param keyIndex The index of the key object in \p nodes
    /// \param pageBox The box of the page (the crop box)
    /// \param containerToPage A regular matrix
    /// \param errorText Translated reason, when no command can be made
    /// \returns nullptr with a text: computeMatrices refused, the counts differ, \p containerToPage
    ///          is not regular. The command itself refuses (in apply) what Transform refuses, and is
    ///          refused as unchanged (PDFContentCommand::isRefusedAsUnchanged), when every matrix is
    ///          the identity or nothing of the nodes moves
    static PDFContentCommandPointer createCommand(Operation operation,
                                                  std::vector<PDFContentNodeReference> nodes,
                                                  const std::vector<QRectF>& boxes,
                                                  size_t keyIndex,
                                                  const QRectF& pageBox,
                                                  const QTransform& containerToPage,
                                                  QString* errorText);

    /// Creates the command from its matrices in the default space of the container of the nodes
    /// (the replay of a script: "ids" and "matrices" of the same length, "operation")
    static PDFContentCommandPointer createCommand(Operation operation,
                                                  std::vector<PDFContentNodeReference> nodes,
                                                  std::vector<QTransform> matrices);
};

}   // namespace pdf

#endif // PDFCONTENTARRANGE_H
