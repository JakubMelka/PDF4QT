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

#ifndef PDFPAGEGEOMETRY_H
#define PDFPAGEGEOMETRY_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfutils.h"

#include <QCoreApplication>
#include <QMarginsF>
#include <QSizeF>
#include <QString>
#include <QPointF>
#include <QTransform>

#include <optional>
#include <vector>

namespace pdf
{

class PDFOperationControl;

/**
 * \brief Configuration for page geometry adjustments performed by PDFPageGeometry.
 *
 * This structure describes how selected pages should be resized, repositioned,
 * and optionally scaled. The operation can target one or more page boxes,
 * derive the placement from an existing reference box, restrict the affected
 * pages by range or subset, and optionally transform annotation geometry
 * together with page content.
 *
 * All user-facing dimensions are expressed in millimeters. They are converted
 * internally to the units of each page (its /UserUnit) before the modification is
 * written back to the document. The sizes, the margins, the anchor and the offset
 * are as the page is shown (its /Rotate applied; the offset Y points up).
 */
struct PDF4QTLIBCORESHARED_EXPORT PDFPageGeometrySettings
{
    /**
     * \brief Restricts the operation to a subset of pages.
     */
    enum class PageSubset
    {
        AllPages,          ///< Apply the operation to all selected pages.
        OddPages,          ///< Apply the operation only to odd-numbered pages.
        EvenPages,         ///< Apply the operation only to even-numbered pages.
        PortraitPages,     ///< Apply the operation only to portrait pages.
        LandscapePages     ///< Apply the operation only to landscape pages.
    };

    /**
     * \brief Selects which page box is used as the source geometry reference.
     */
    enum class ReferenceBox
    {
        MediaBox,   ///< Use the page media box.
        CropBox,    ///< Use the page crop box.
        BleedBox,   ///< Use the page bleed box.
        TrimBox,    ///< Use the page trim box.
        ArtBox      ///< Use the page art box.
    };

    /**
     * \brief Defines the alignment anchor inside the target inner rectangle.
     *
     * The anchor is used when the referenced source box is placed into the
     * target area after margins are applied.
     */
    enum class Anchor
    {
        TopLeft,        ///< Align to the top-left corner.
        TopCenter,      ///< Align to the top edge center.
        TopRight,       ///< Align to the top-right corner.
        MiddleLeft,     ///< Align to the left edge center.
        MiddleCenter,   ///< Align to the center.
        MiddleRight,    ///< Align to the right edge center.
        BottomLeft,     ///< Align to the bottom-left corner.
        BottomCenter,   ///< Align to the bottom edge center.
        BottomRight     ///< Align to the bottom-right corner.
    };

    QString pageRange = "-";                          ///< Page range in the standard PDF4QT interval syntax.
    bool isRangeLimitedToDocument = false;           ///< The pages of the range beyond the document are left out (PageMaster applies the settings to each output of an export, B27); false = such a range is an error (B46)
    PageSubset pageSubset = PageSubset::AllPages;    ///< Additional subset filter applied after the page range.
    ReferenceBox referenceBox = ReferenceBox::CropBox; ///< Source page box used to compute placement and scaling.

    bool applyMediaBox = true;   ///< Rewrite the media box.
    bool applyCropBox = true;    ///< Rewrite the crop box.
    bool applyBleedBox = false;  ///< Rewrite the bleed box.
    bool applyTrimBox = false;   ///< Rewrite the trim box.
    bool applyArtBox = false;    ///< Rewrite the art box.

    bool useTargetPageSize = false;                      ///< Use targetPageSizeMM instead of deriving the target size from the source box and margins.
    QSizeF targetPageSizeMM = QSizeF(210.0, 297.0);     ///< Explicit output page size in millimeters.
    QMarginsF marginsMM = QMarginsF(0.0, 0.0, 0.0, 0.0); ///< Inner margins in millimeters, ordered as left, top, right, bottom.

    Anchor anchor = Anchor::MiddleCenter;             ///< Anchor used to place the transformed source box into the target inner rectangle.
    QPointF offsetMM = QPointF(0.0, 0.0);             ///< Additional placement offset in millimeters.

    bool scaleContent = false;                        ///< Scale page content to fit the target inner rectangle.
    bool preserveAspectRatio = true;                  ///< Preserve the original aspect ratio when scaleContent is enabled.
    bool scaleAnnotationsAndFormFields = true;        ///< Transform annotation and widget geometry together with scaled page content.

    /**
     * \brief Returns true if at least one output page box is selected.
     */
    bool hasAnyTargetBoxSelected() const
    {
        return applyMediaBox || applyCropBox || applyBleedBox || applyTrimBox || applyArtBox;
    }
};

/// One change of the geometry of a page (PDFPageGeometry::applyChanges). The boxes are in the
/// default user space of the page after the change (the units of the page); a box, which is not
/// given, stays as it is. A content transform moves the content of the page (default user space
/// before the change to the one after it); everything, which refers to positions on the page,
/// follows it (plan of step 3 of phase 2, 5.4).
class PDF4QTLIBCORESHARED_EXPORT PDFPageGeometryChange
{
public:
    PDFInteger pageIndex = -1;          ///< Page (index from 0)
    std::optional<QRectF> mediaBox;     ///< New /MediaBox
    std::optional<QRectF> cropBox;      ///< New /CropBox
    std::optional<QRectF> bleedBox;     ///< New /BleedBox
    std::optional<QRectF> trimBox;      ///< New /TrimBox
    std::optional<QRectF> artBox;       ///< New /ArtBox

    /// The content moves by this matrix (it has no rotation: the page keeps its /Rotate)
    std::optional<QTransform> contentTransform;

    /// The old content is clipped to this rectangle (default user space before the change);
    /// empty = no clip. What was hidden by the old crop box stays hidden.
    QRectF contentClip;

    /// With a content transform: the annotations are scaled with the content (/Rect and every key
    /// of their geometry); false = they only move with the content (their size stays)
    bool scaleAnnotations = true;

    bool hasContentTransform() const { return contentTransform.has_value() && !contentTransform->isIdentity(); }
};

/// What a change of the geometry has changed
class PDF4QTLIBCORESHARED_EXPORT PDFPageGeometryStatistics
{
public:
    int pagesChanged = 0;               ///< Pages with a new box or content
    int contentsTransformed = 0;        ///< Pages, whose content was scaled, moved or clipped
    int annotationsTransformed = 0;     ///< Annotations, whose /Rect changed
    int destinationsTransformed = 0;    ///< Explicit destinations moved with their pages
    int viewportsTransformed = 0;       ///< Viewports (/VP) moved with their pages
    int measuresTransformed = 0;        ///< Measure dictionaries scaled
    int beadsTransformed = 0;           ///< Article beads moved with their pages
    int patternsTransformed = 0;        ///< Patterns of the page resources following the content
    int structureBoxesTransformed = 0;  ///< /BBox of the layout attributes of structure elements
    int thumbnailsRemoved = 0;          ///< /Thumb of the changed pages (12.3.4: it would not match)
};

/**
 * \brief Applies page geometry transformations to a PDF document.
 *
 * The geometry of a page is worked in the coordinates, in which the page is shown: its /Rotate
 * applied (ISO 32000-2, Table 31), points or millimeters with /UserUnit, the origin at the top
 * left corner of the shown box. The page itself never changes its /Rotate. Every box, which is
 * written, is written into the dictionary of the page (an inherited value of the page tree stays
 * for the other pages, 7.7.3.4).
 *
 * The functions are reentrant: they read their arguments and return new values.
 */
class PDF4QTLIBCORESHARED_EXPORT PDFPageGeometry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFPageGeometry)

public:
    using Box = PDFPageGeometrySettings::ReferenceBox;

    /// Limits of a side of a written box in the units of the page (PDF Reference 1.7, Table C.1)
    static constexpr PDFReal MINIMUM_SIDE = 3.0;
    static constexpr PDFReal MAXIMUM_SIDE = 14400.0;

    // ---------------------------------------------------------------- the coordinates

    /// The box as it takes effect (14.11.2): the media box, the crop box reduced to the media
    /// box, the bleed, trim and art boxes (default: the crop box) reduced to the media box
    static QRectF getEffectiveBox(const PDFPage* page, Box box);

    /// The box is written in the dictionary of the page (the media box and the crop box also in
    /// an ancestor of the page tree, they are inheritable); the other boxes take their default
    static bool isBoxDefined(const PDFPage* page, const PDFObjectStorage* storage, Box box);

    /// Matrix of the coordinates of \p box as it is shown with \p rotation (units of the page, the
    /// origin at the top left corner, y down) to the default user space; the inverse of the matrix
    /// of the renderer (PDFRenderer::createMediaBoxToDevicePointMatrix) at the scale 1
    static QTransform getShownToUserTransform(const QRectF& box, PageRotation rotation);

    /// Margins as they are shown (left, top, right, bottom) as the margins of the unrotated box of
    /// a page of \p rotation (/Rotate 90: the shown left margin is the bottom one)
    static QMarginsF getUnrotatedMargins(const QMarginsF& shownMargins, PageRotation rotation);

    /// The rectangle is finite and each side is within MINIMUM_SIDE to MAXIMUM_SIDE
    static bool isValidBox(const QRectF& box);

    /// Names of a box for the parameters ("media", "crop", "bleed", "trim", "art") and back
    static QString getBoxName(Box box);
    static std::optional<Box> getBoxByName(const QString& name);

    /// All boxes in the order media, crop, bleed, trim, art
    static std::vector<Box> getAllBoxes();

    // ---------------------------------------------------------------- Crop Pages (5.3)

    enum class CropMode
    {
        EqualMargins,   ///< The same margin is cut from all sides of the visible area
        CustomBox,      ///< A box of the shown reference page; the other pages get its margins
        Remove          ///< The crop box becomes the media box
    };

    struct CropSettings
    {
        CropMode mode = CropMode::EqualMargins;
        PDFReal marginMM = 10.0;    ///< EqualMargins
        QRectF boxMM;               ///< CustomBox: left, top, width and height as the reference page is shown, millimeters
    };

    /// The new crop box of \p page (default user space). \p referencePage is the page of the
    /// custom box (nullptr = \p page): the box is its shown visible area without the margins of
    /// the box (a page of another size gets the same margins, as PageMaster does). std::nullopt,
    /// when a margin is not a finite number or the visible area would have a side shorter than
    /// MINIMUM_SIDE (the tool says which pages).
    static std::optional<QRectF> computeCropBox(const PDFPage* page, const PDFPage* referencePage, const CropSettings& settings);

    /// The shown size of the visible area of the page in millimeters
    static QSizeF getShownSizeMM(const PDFPage* page);

    // ---------------------------------------------------------------- Resize Pages (5.4)

    enum class Orientation
    {
        Auto,       ///< The sheet lies as the page is shown
        Portrait,
        Landscape
    };

    struct ResizeSettings
    {
        std::optional<QSizeF> pageSizeMM;   ///< The sheet in millimeters (portrait for a preset); std::nullopt = the size of the page
        bool isCustomSize = false;          ///< The sheet is given as it is shown (the orientation is not used)
        Orientation orientation = Orientation::Auto;
        bool scaleContent = true;           ///< Scale to fit; false = Keep size (only the boxes move)
        Qt::Alignment anchor = Qt::AlignCenter;
        QMarginsF marginsMM;                ///< As shown: left, top, right, bottom
        QPointF offsetMM;                   ///< As shown: x to the right, y down
        bool preserveAspectRatio = true;
        bool scaleAnnotations = true;
    };

    /// Where the content of a page goes: everything in the shown coordinates of the new sheet
    /// (units of the page) and the change of the page
    struct ResizePlacement
    {
        bool isValid = false;
        QString errorMessage;           ///< Translated reason of an invalid placement
        QSizeF sheetSize;               ///< The new sheet as shown
        QRectF innerRect;               ///< The sheet without the margins
        QRectF contentRect;             ///< The visible area of the page placed and scaled
        PDFReal scaleX = 1.0;           ///< Scale of the content (as shown)
        PDFReal scaleY = 1.0;
        PDFPageGeometryChange change;   ///< The change of the page (without its index)
    };

    /// Placement of a page resized by \p settings (Letter to A4 with 15 mm margins: the scale
    /// min(180 / 215.9, 267 / 279.4) = 0.834). The visible area of the page is what is placed;
    /// with Scale to fit the sheet is the new media and crop box and the content is clipped to the
    /// old visible area; with Keep size the media box moves around the content. The bleed, trim
    /// and art boxes, which the page has (\p storage tells it; without it a box equal to the crop
    /// box is taken as its default), follow the content and are reduced to the new media box.
    static ResizePlacement computeResize(const PDFPage* page, const ResizeSettings& settings, const PDFObjectStorage* storage = nullptr);

    // ---------------------------------------------------------------- Set Page Boxes (5.5)

    /// The new box: the effective \p basedOn box with the margins added (as shown; positive =
    /// larger, negative = smaller), default user space
    static QRectF computeBox(const PDFPage* page, Box basedOn, const QMarginsF& marginsMM);

    // ---------------------------------------------------------------- the engine

    /// Writes the changes of the pages into a copy of the document, which keeps the numbers of
    /// its objects (an incremental update; the pages, their content streams, annotations and
    /// destinations are rewritten in place, new streams are new objects). With a content
    /// transform: a new stream "q <matrix> cm [clip] q..." before the content and the closing
    /// "Q" after it (the excess "Q" of hostile content cannot undo the matrix, 8.4.2), the
    /// annotations (12.5.2 and the tables of the types), the viewports and measures (12.9), the
    /// explicit destinations to the page (12.3.2.2, also of the structure destinations), the
    /// beads (12.4.3), the patterns of the resources of the page (8.7.2: their matrix maps to the
    /// default space of the page) and the layout boxes of the structure (14.8.5.4.3) follow the
    /// content. /Thumb of a changed page is removed (12.3.4). Throws PDFException for an invalid
    /// page index; returns the input copy unchanged for no change.
    static PDFDocument applyChanges(const PDFDocument* document,
                                    const std::vector<PDFPageGeometryChange>& changes,
                                    const PDFOperationControl* control,
                                    PDFPageGeometryStatistics* statistics);

    /// The changes move or scale an annotation (its /Rect changes): bit 6 of the permissions
    /// (Table 22, R4)
    static bool isAnnotationChanged(const PDFDocument* document, const std::vector<PDFPageGeometryChange>& changes);

    /**
     * \brief Applies page geometry settings to a document (the dialog Page Geometry of the
     *        Editor and PageMaster). The selected boxes become the new sheet; with scaled
     *        content the other boxes follow the content, without it they stay.
     * \param document Document to modify.
     * \param settings Geometry settings describing the requested transformation.
     * \param modificationFlags Optional output flags describing what parts of the
     *        document were modified.
     * \return Operation result. On failure, the result contains a translated
     *         error message suitable for presentation to the user.
     */
    static PDFOperationResult apply(PDFDocument* document,
                                    const PDFPageGeometrySettings& settings,
                                    PDFModifiedDocument::ModificationFlags* modificationFlags = nullptr);
};

}   // namespace pdf

#endif // PDFPAGEGEOMETRY_H
