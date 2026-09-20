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

#ifndef PDFOCRMODEL_H
#define PDFOCRMODEL_H

#include "pdfglobal.h"

#include <QRectF>
#include <QLineF>
#include <QString>
#include <QPointF>
#include <QPolygonF>
#include <QDateTime>
#include <QTransform>
#include <QStringList>
#include <QVariantMap>

#include <array>
#include <vector>
#include <optional>

namespace pdf
{

// -------------------------------------------------------------------------
// Engine independent OCR data model (OCR specification, chapter 12.4).
//
// Coordinate convention (GEOM-01, GEOM-02):
//   * Canonical space of all results is the unrotated default user space
//     of the PDF page, in PDF units (1 unit = UserUnit / 72 inch). The origin
//     and the orientation of the axes are the ones of the document, y grows
//     upwards.
//   * Raster space: pixel coordinates of the rendered page image, x grows to
//     the right, y grows downwards, origin in the top-left pixel corner.
//   * Engine space: pixel coordinates of the image passed to the OCR engine
//     (after preprocessing).
//   * Transformations are QTransform matrices with Qt convention, i.e. a point
//     p is transformed as p' = p * T, and the composition T1 * T2 applies T1
//     first and then T2. PDFOCRPageGeometry::pageToRaster is the matrix R,
//     PDFOCRPageGeometry::rasterToEngine is the matrix P. A point o reported
//     by the engine is mapped to the page as
//         p_pdf = o * P^-1 * R^-1 = o * (R * P)^-1.
//     Whole quadrilaterals and baselines are transformed, never only the
//     top-left corner.
// -------------------------------------------------------------------------

/// Score granularity reported by the engine (CONF-01). A word level score
/// is a true per-word score, a line level score is shared by all words of
/// the line and must be displayed as such.
enum class PDFOCRConfidenceLevel
{
    Unknown,    ///< No score available
    Symbol,
    Word,
    Line,
    Block,
    Page
};

/// Confidence of a recognized unit (CONF-01, CONF-02). The normalized score
/// is 0-100, missing score is represented by an empty optional (never by zero).
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRConfidence
{
    /// Normalized score 0-100, or empty, if score is not available
    std::optional<double> normalized;

    /// Raw engine score, or empty, if score is not available
    std::optional<double> raw;

    /// Range of the raw score
    double rawMinimum = 0.0;
    double rawMaximum = 100.0;

    /// Granularity of the score
    PDFOCRConfidenceLevel level = PDFOCRConfidenceLevel::Unknown;

    bool isAvailable() const { return normalized.has_value(); }

    /// Creates confidence from raw score with given range. Values outside of
    /// the range are clamped. Non-finite values produce unknown confidence.
    static PDFOCRConfidence fromRaw(double rawValue, double rawMinimum, double rawMaximum, PDFOCRConfidenceLevel level);

    /// Creates unknown confidence
    static PDFOCRConfidence unknown() { return PDFOCRConfidence(); }

    bool operator==(const PDFOCRConfidence&) const = default;
};

/// Quadrilateral in canonical page space. Points are ordered
/// bottom-left, bottom-right, top-right, top-left, where "bottom" is the
/// side of the baseline (the descender side) and "left" is the start
/// of the text in writing direction. Bounding rectangle is a derived value.
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRQuad
{
    std::array<QPointF, 4> points = { };

    static PDFOCRQuad fromRect(const QRectF& rect);
    static PDFOCRQuad fromPolygon(const QPolygonF& polygon);

    QRectF boundingRect() const;
    QPolygonF toPolygon() const;
    PDFOCRQuad transformed(const QTransform& transform) const;

    /// Returns true, if all coordinates are finite and the quad is
    /// not degenerated (both the width and the height are positive).
    bool isValid() const;

    /// Length of the bottom edge (width in writing direction)
    qreal width() const;

    /// Distance between the bottom and the top edge (height perpendicular to the baseline)
    qreal height() const;

    /// Bottom-left corner
    const QPointF& origin() const { return points[0]; }

    /// Unit direction of the bottom edge (writing direction)
    QPointF direction() const;

    /// Angle of the writing direction in degrees, counterclockwise
    /// in the canonical space (0 = text runs along +x)
    qreal angle() const;

    /// Translates the quad by given offset
    PDFOCRQuad translated(const QPointF& offset) const;

    bool operator==(const PDFOCRQuad&) const = default;
};

/// Origin of the geometry of the unit
enum class PDFOCRGeometryOrigin
{
    Engine,     ///< Geometry returned by the engine
    Estimated,  ///< Geometry estimated by the editor (split, extension, ...)
    Manual,     ///< Geometry edited by the user
    Imported,   ///< Geometry read from the document (own OCR layer) or project
    Digital     ///< Geometry of digital text of the document
};

/// Origin of the text of the unit
enum class PDFOCRTextOrigin
{
    OCR,        ///< Text recognized by the engine
    Manual,     ///< Text inserted by the user
    Imported,   ///< Text read from the document (own OCR layer) or project
    Digital     ///< Digital text of the document
};

/// Review state of the unit (DATA-01). Independent of the engine score.
enum class PDFOCRReviewState
{
    Unreviewed,     ///< Not yet checked by the user
    Confirmed,      ///< Confirmed by the user (score is not modified)
    Modified,       ///< Text or geometry modified by the user
    Discarded       ///< Marked as "not text", must not be written into the document
};

/// Text direction of a line
enum class PDFOCRTextDirection
{
    LeftToRight,
    RightToLeft,
    TopToBottom
};

/// Word or text segment (DATA-01). Where words are not a natural unit
/// of the language, it is a text segment.
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRWord
{
    int id = 0;

    /// Text originally recognized by the engine (historical, never modified by editing)
    QString originalText;

    /// Current text
    QString text;

    /// Geometry in the canonical page space
    PDFOCRQuad quad;

    PDFOCRGeometryOrigin geometryOrigin = PDFOCRGeometryOrigin::Engine;
    PDFOCRTextOrigin textOrigin = PDFOCRTextOrigin::OCR;

    /// Language of the word (engine dependent code, e.g. "ces")
    QString language;

    /// Score of the original recognition (historical)
    PDFOCRConfidence confidence;

    PDFOCRReviewState reviewState = PDFOCRReviewState::Unreviewed;

    /// Optional time of the review
    std::optional<QDateTime> reviewTime;

    /// Identifiers of predecessors (for merged/split words)
    std::vector<int> predecessorIds;

    /// Word overlaps an excluded region (REGION-05); must be reviewed, never
    /// written into the document automatically.
    bool overlapsExcludedRegion = false;

    /// Horizontal scaling of the text into the geometry is extreme (EDIT-04)
    bool hasExtremeScaling = false;

    /// Returns true, if the word is usable for the text layer (nonempty
    /// text, valid geometry, not discarded)
    bool isUsable() const;

    /// Returns true, if the word was edited (text differs from original)
    bool isTextModified() const { return text != originalText; }

    bool operator==(const PDFOCRWord&) const = default;
};

/// Text line (DATA-01)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRLine
{
    int id = 0;

    /// Baseline in canonical page space (from the start to the end of the line)
    QLineF baseline;

    /// Geometry of the whole line
    PDFOCRQuad quad;

    PDFOCRTextDirection direction = PDFOCRTextDirection::LeftToRight;

    /// Line level confidence (used, when engine does not provide word confidences)
    PDFOCRConfidence confidence;

    std::vector<PDFOCRWord> words;

    /// Logical text of the line (words separated by single space, discarded words are skipped)
    QString getText() const;

    /// Recomputes the geometry of the line from its words
    void updateGeometryFromWords();

    bool operator==(const PDFOCRLine&) const = default;
};

/// Type of the block
enum class PDFOCRBlockType
{
    Text,
    Table,
    Image,
    Separator,
    Other
};

/// Block of text (DATA-01)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRBlock
{
    int id = 0;

    PDFOCRBlockType type = PDFOCRBlockType::Text;

    /// Geometry of the block
    PDFOCRQuad quad;

    /// Region from which this block originates, or -1 for the whole page
    int regionId = -1;

    std::vector<PDFOCRLine> lines;

    /// Text of the block, lines separated by '\n'
    QString getText() const;

    /// Recomputes the geometry of the block from its lines
    void updateGeometryFromLines();

    bool operator==(const PDFOCRBlock&) const = default;
};

/// Type of the region (REGION-01)
enum class PDFOCRRegionType
{
    Recognize,  ///< Inclusive region
    Exclude     ///< Excluded region
};

/// Per-region override of the recognition settings (PAGE-06, REGION-02)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRRegionOverride
{
    /// Languages (empty = inherit)
    QStringList languages;

    /// Layout (page segmentation) override, -1 = inherit. Engine dependent value,
    /// for Tesseract it is the PSM number.
    int segmentation = -1;

    /// Rotation override in degrees (0, 90, 180, 270) or -1 = inherit
    int rotation = -1;

    bool isEmpty() const { return languages.isEmpty() && segmentation == -1 && rotation == -1; }

    bool operator==(const PDFOCRRegionOverride&) const = default;
};

/// User or analyzer defined region of the page (REGION-01..05)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRRegion
{
    int id = 0;
    PDFOCRRegionType type = PDFOCRRegionType::Recognize;
    QString name;

    /// Rectangle in canonical page space
    QRectF rect;

    /// Reading order of the region (lower first)
    int order = 0;

    /// Configuration exception of the region
    PDFOCRRegionOverride configuration;

    /// Region was proposed by the layout analysis
    bool proposedByAnalysis = false;

    bool operator==(const PDFOCRRegion&) const = default;
};

/// Machine readable error codes (JOB-08)
enum class PDFOCRErrorCode
{
    None,
    MissingModel,
    IncompatibleModel,
    InitializationFailed,
    InvalidConfiguration,
    InsufficientPermissions,
    RasterizationFailed,
    ImageTooLarge,
    OutOfMemory,
    OutOfDiskSpace,
    Timeout,
    Cancelled,
    WorkerCrashed,
    InvalidEngineOutput,
    IrreversibleTransformation,
    DocumentRevisionConflict,
    WriteFailed,
    ExportFailed,
    NetworkError,
    VerificationFailed,
    Unknown
};

/// Error description with machine readable code, human readable message,
/// the step, in which the error occured, and optional technical detail.
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRError
{
    PDFOCRErrorCode code = PDFOCRErrorCode::None;
    QString message;
    QString step;
    QString detail;

    bool isError() const { return code != PDFOCRErrorCode::None; }
    explicit operator bool() const { return isError(); }

    static PDFOCRError none() { return PDFOCRError(); }
    static PDFOCRError create(PDFOCRErrorCode code, QString message, QString step = QString(), QString detail = QString());

    /// Returns stable machine readable identifier of the code
    static QString getCodeIdentifier(PDFOCRErrorCode code);

    /// Parses the code identifier, returns Unknown for unrecognized code
    static PDFOCRErrorCode parseCodeIdentifier(const QString& identifier);

    bool operator==(const PDFOCRError&) const = default;
};

/// Primary state of the page result (JOB-03). Exactly one state is active.
enum class PDFOCRPageState
{
    Pending,
    Preparing,
    Recognizing,
    Done,
    NoText,
    Skipped,
    Error,
    Cancelled,
    Stale
};

/// Classification of the page content (INPUT-02)
enum class PDFOCRPageContentClass
{
    Unknown,
    Image,          ///< Only images, no usable text
    VisibleText,    ///< Page with usable visible text
    InvisibleText,  ///< Page with invisible text (OCR layer of foreign or own origin)
    Mixed,          ///< Both image and text content
    Empty,          ///< Empty page
    Ambiguous       ///< Cannot be decided, see reasons
};

/// Result of the page analysis (INPUT-01..03)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRPageAnalysis
{
    PDFOCRPageContentClass contentClass = PDFOCRPageContentClass::Unknown;

    bool hasImages = false;
    bool hasVisibleText = false;
    bool hasInvisibleText = false;
    bool hasOwnOCRLayer = false;
    bool hasAnnotations = false;
    bool hasFormFields = false;
    bool hasUnappliedRedactions = false;
    bool isTagged = false;

    /// Number of visible text characters
    int visibleCharacterCount = 0;

    /// Number of invisible text characters
    int invisibleCharacterCount = 0;

    /// Number of images
    int imageCount = 0;

    /// Rectangles covered by annotations (canonical page space)
    std::vector<QRectF> annotationRectangles;

    /// Rectangles of unapplied redaction annotations (canonical page space)
    std::vector<QRectF> redactionRectangles;

    /// Reasons of the ambiguity (human readable)
    QStringList ambiguityReasons;

    /// Additional notes about the page content (human readable)
    QStringList notes;

    /// Identifier of the own OCR layer, if present
    QString ownLayerId;

    /// Number of characters without unicode mapping
    int unmappedCharacterCount = 0;

    /// Returns true, if page has usable visible text
    bool hasUsableVisibleText() const { return contentClass == PDFOCRPageContentClass::VisibleText || contentClass == PDFOCRPageContentClass::Mixed; }

    bool operator==(const PDFOCRPageAnalysis&) const = default;
};

/// Geometry of the page and of the working raster (GEOM-01..03, DATA-01)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRPageGeometry
{
    QRectF mediaBox;
    QRectF cropBox;

    /// Page rotation in degrees (0, 90, 180, 270)
    int rotation = 0;

    /// User unit of the page
    double userUnit = 1.0;

    /// Size of the working raster in pixels
    QSize rasterSize;

    /// Resolution of the working raster in DPI (actually used)
    double dpi = 0.0;

    /// Requested DPI (can differ from actual, if limited)
    double requestedDpi = 0.0;

    /// Transformation R, canonical page space -> raster space
    QTransform pageToRaster;

    /// Transformation P, raster space -> engine input space
    QTransform rasterToEngine;

    /// Size of the engine input image
    QSize engineImageSize;

    /// Recorded preprocessing pipeline (IMAGE-05)
    QStringList pipeline;

    /// Returns transformation from the engine space to the canonical page space
    QTransform getEngineToPage() const;

    /// Returns transformation from the canonical page space to the engine space
    QTransform getPageToEngine() const { return pageToRaster * rasterToEngine; }

    /// Returns true, if both transformations are invertible
    bool isInvertible() const;

    bool operator==(const PDFOCRPageGeometry&) const = default;
};

/// Provenance of the results (DATA-01)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRProvenance
{
    QString engineId;
    QString engineVersion;

    /// Identifiers of the used models (e.g. "tesseract/fast/ces")
    QStringList modelIds;

    /// Hash of the resolved model set
    QString modelSetHash;

    /// Effective parameters
    QVariantMap parameters;

    bool operator==(const PDFOCRProvenance&) const = default;
};

/// Orientation detection result (IMAGE-04)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCROrientation
{
    /// Rotation in degrees (0, 90, 180, 270), which must be applied to the
    /// image to make the text upright
    int rotation = 0;

    /// Confidence of the detection (engine specific, 0-100 normalized), or empty
    std::optional<double> confidence;

    /// Deskew angle in degrees (counterclockwise), 0 if not available
    double deskewAngle = 0.0;

    /// Confidence of the deskew angle
    std::optional<double> deskewConfidence;

    /// Detected script name (if available)
    QString script;

    bool operator==(const PDFOCROrientation&) const = default;
};

/// Result of one page (DATA-01)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRPageResult
{
    /// Physical page index (0-based)
    PDFInteger pageIndex = -1;

    /// Page label (as displayed), may be empty
    QString pageLabel;

    /// Fingerprint of the page content
    QByteArray pageFingerprint;

    PDFOCRPageState state = PDFOCRPageState::Pending;
    PDFOCRError error;

    /// Reason of skipping (human readable), if state is Skipped
    QString skipReason;

    PDFOCRPageAnalysis analysis;
    PDFOCRPageGeometry geometry;
    PDFOCRProvenance provenance;
    std::optional<PDFOCROrientation> orientation;

    /// Regions of the page
    std::vector<PDFOCRRegion> regions;

    /// Blocks in reading order
    std::vector<PDFOCRBlock> blocks;

    /// Generation of the recognition (increases with each recognition run)
    int generation = 0;

    /// Next free identifier
    int nextId = 1;

    /// Time of the recognition
    QDateTime recognitionTime;

    /// Elapsed time in milliseconds
    qint64 elapsedMilliseconds = 0;

    /// Blank page detection was overriden by the user
    bool blankDetectionOverridden = false;

    /// Local modification flag (results were edited after recognition)
    bool isModified = false;

    /// Allocates a new identifier
    int allocateId() { return nextId++; }

    /// Returns true, if page has a valid result (recognized or without text)
    bool hasResult() const { return state == PDFOCRPageState::Done || state == PDFOCRPageState::NoText; }

    /// Returns number of words (not discarded)
    int getWordCount() const;

    /// Returns all words in reading order
    std::vector<const PDFOCRWord*> getWords() const;
    std::vector<PDFOCRWord*> getWords();

    /// Finds word by identifier, or nullptr
    const PDFOCRWord* findWord(int id) const;
    PDFOCRWord* findWord(int id);

    /// Finds line by identifier, or nullptr
    const PDFOCRLine* findLine(int id) const;
    PDFOCRLine* findLine(int id);

    /// Finds line containing given word, or nullptr
    const PDFOCRLine* findLineOfWord(int wordId) const;
    PDFOCRLine* findLineOfWord(int wordId);

    /// Finds block by identifier, or nullptr
    const PDFOCRBlock* findBlock(int id) const;
    PDFOCRBlock* findBlock(int id);

    /// Finds block containing given line, or nullptr
    PDFOCRBlock* findBlockOfLine(int lineId);

    /// Finds region by identifier, or nullptr
    const PDFOCRRegion* findRegion(int id) const;
    PDFOCRRegion* findRegion(int id);

    /// Returns text of the page in reading order. Blocks are separated
    /// by an empty line, lines by '\n'.
    QString getText() const;

    /// Assigns identifiers to all units without an identifier and
    /// ensures uniqueness of identifiers.
    void assignIdentifiers();

    /// Returns true, if page contains at least one usable word
    bool hasUsableText() const;

    bool operator==(const PDFOCRPageResult&) const = default;
};

/// Statistics of the confidence (CONF-04)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRConfidenceStatistics
{
    /// Total number of words (not discarded)
    int wordCount = 0;

    /// Number of words with available score
    int scoredWordCount = 0;

    /// Number of words with unknown score
    int unknownWordCount = 0;

    /// Number of manually added words
    int manualWordCount = 0;

    /// Number of words below the threshold (with available score)
    int belowThresholdCount = 0;

    /// Number of unreviewed words
    int unreviewedCount = 0;

    /// Number of modified words
    int modifiedCount = 0;

    /// Number of confirmed words
    int confirmedCount = 0;

    /// Number of discarded words
    int discardedCount = 0;

    /// Number of words requiring a review
    int reviewRequiredCount = 0;

    /// Arithmetic mean of the original scores of words with available score
    /// (scores of manually added or edited words are historical and not included
    /// in the sense of CONF-05, so mean is computed from original engine scores only).
    std::optional<double> meanScore;

    /// Level of the scores used for the statistics
    PDFOCRConfidenceLevel level = PDFOCRConfidenceLevel::Unknown;

    /// Computes statistics of the page
    static PDFOCRConfidenceStatistics compute(const PDFOCRPageResult& page, double threshold);

    /// Merges statistics of several pages
    void merge(const PDFOCRConfidenceStatistics& other);

private:
    double m_scoreSum = 0.0;
};

/// Helper functions for review decisions (CONF-03)
class PDF4QTLIBCORESHARED_EXPORT PDFOCRReview
{
public:
    /// Returns true, if word requires a review: its score is below threshold,
    /// its score is unknown, it was manually inserted and not confirmed,
    /// or it overlaps an excluded region.
    static bool requiresReview(const PDFOCRWord& word, double threshold);

    /// Returns true, if score is below threshold (unknown score is not below threshold)
    static bool isBelowThreshold(const PDFOCRConfidence& confidence, double threshold);
};

/// Validation of the results before writing (DATA-03)
class PDF4QTLIBCORESHARED_EXPORT PDFOCRValidator
{
public:
    struct Limits
    {
        int maximumWordsPerPage = 100000;
        int maximumTextLength = 4096;
        double coordinateLimit = 1.0e6;
    };

    /// Validates the page result. Returns list of errors (empty, if valid).
    static QStringList validate(const PDFOCRPageResult& page, const Limits& limits = Limits());

    /// Returns true, if the text is valid unicode (no unpaired surrogates, no NUL characters)
    static bool isValidText(const QString& text);

    /// Returns true, if the rectangle is finite and has positive size
    static bool isValidRect(const QRectF& rect, double coordinateLimit);
};

/// Conversion of the enumerations to string identifiers (for serialization)
class PDF4QTLIBCORESHARED_EXPORT PDFOCREnumerations
{
public:
    static QString toString(PDFOCRPageState value);
    static QString toString(PDFOCRReviewState value);
    static QString toString(PDFOCRGeometryOrigin value);
    static QString toString(PDFOCRTextOrigin value);
    static QString toString(PDFOCRConfidenceLevel value);
    static QString toString(PDFOCRBlockType value);
    static QString toString(PDFOCRRegionType value);
    static QString toString(PDFOCRTextDirection value);
    static QString toString(PDFOCRPageContentClass value);

    static PDFOCRPageState toPageState(const QString& value);
    static PDFOCRReviewState toReviewState(const QString& value);
    static PDFOCRGeometryOrigin toGeometryOrigin(const QString& value);
    static PDFOCRTextOrigin toTextOrigin(const QString& value);
    static PDFOCRConfidenceLevel toConfidenceLevel(const QString& value);
    static PDFOCRBlockType toBlockType(const QString& value);
    static PDFOCRRegionType toRegionType(const QString& value);
    static PDFOCRTextDirection toTextDirection(const QString& value);
    static PDFOCRPageContentClass toPageContentClass(const QString& value);
};

}   // namespace pdf

#endif // PDFOCRMODEL_H
