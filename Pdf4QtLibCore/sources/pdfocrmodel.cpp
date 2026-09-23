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

#include "pdfocrmodel.h"

#include <QSet>
#include <QtMath>

#include <cmath>
#include <algorithm>

namespace pdf
{

// -------------------------------------------------------------------------
// PDFOCRConfidence
// -------------------------------------------------------------------------

PDFOCRConfidence PDFOCRConfidence::fromRaw(double rawValue, double rawMinimum, double rawMaximum, PDFOCRConfidenceLevel level)
{
    PDFOCRConfidence result;

    if (!std::isfinite(rawValue) || !std::isfinite(rawMinimum) || !std::isfinite(rawMaximum) || rawMaximum <= rawMinimum)
    {
        return result;
    }

    // Values below the minimum (for example -1 in the non-text rows of the Tesseract
    // TSV output) are not scores; they are reported as unknown, never as zero (CONF-02).
    if (rawValue < rawMinimum)
    {
        return result;
    }

    result.raw = rawValue;
    result.rawMinimum = rawMinimum;
    result.rawMaximum = rawMaximum;
    result.level = level;

    const double clamped = std::clamp(rawValue, rawMinimum, rawMaximum);
    result.normalized = 100.0 * (clamped - rawMinimum) / (rawMaximum - rawMinimum);
    return result;
}

// -------------------------------------------------------------------------
// PDFOCRQuad
// -------------------------------------------------------------------------

PDFOCRQuad PDFOCRQuad::fromRect(const QRectF& rect)
{
    // Canonical page space has y axis growing upwards, so bottom of the
    // text box is the minimal y coordinate.
    PDFOCRQuad quad;
    const qreal left = qMin(rect.left(), rect.right());
    const qreal right = qMax(rect.left(), rect.right());
    const qreal bottom = qMin(rect.top(), rect.bottom());
    const qreal top = qMax(rect.top(), rect.bottom());
    quad.points[0] = QPointF(left, bottom);
    quad.points[1] = QPointF(right, bottom);
    quad.points[2] = QPointF(right, top);
    quad.points[3] = QPointF(left, top);
    return quad;
}

PDFOCRQuad PDFOCRQuad::fromPolygon(const QPolygonF& polygon)
{
    PDFOCRQuad quad;

    if (polygon.size() >= 4)
    {
        for (int i = 0; i < 4; ++i)
        {
            quad.points[i] = polygon[i];
        }
    }
    else if (!polygon.isEmpty())
    {
        quad = fromRect(polygon.boundingRect());
    }

    return quad;
}

QRectF PDFOCRQuad::boundingRect() const
{
    return toPolygon().boundingRect();
}

QPolygonF PDFOCRQuad::toPolygon() const
{
    QPolygonF polygon;
    polygon.reserve(4);
    for (const QPointF& point : points)
    {
        polygon << point;
    }
    return polygon;
}

PDFOCRQuad PDFOCRQuad::transformed(const QTransform& transform) const
{
    PDFOCRQuad quad;
    for (size_t i = 0; i < points.size(); ++i)
    {
        quad.points[i] = transform.map(points[i]);
    }
    return quad;
}

bool PDFOCRQuad::isValid() const
{
    for (const QPointF& point : points)
    {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
        {
            return false;
        }
    }

    return width() > 0.0 && height() > 0.0;
}

qreal PDFOCRQuad::width() const
{
    return QLineF(points[0], points[1]).length();
}

qreal PDFOCRQuad::height() const
{
    // Distance of the top-left point from the bottom edge line
    const QPointF direction = this->direction();
    const QPointF normal(-direction.y(), direction.x());
    const QPointF offset = points[3] - points[0];
    return std::abs(QPointF::dotProduct(offset, normal));
}

PDFOCRQuad PDFOCRQuad::fromOrientedBounds(const QPointF& direction, const std::vector<PDFOCRQuad>& quads)
{
    const qreal length = std::hypot(direction.x(), direction.y());
    const QPointF unitDirection = (std::isfinite(length) && !qFuzzyIsNull(length)) ? direction / length : QPointF(1.0, 0.0);
    const QPointF unitNormal(-unitDirection.y(), unitDirection.x());

    bool hasPoint = false;
    qreal minU = 0.0;
    qreal maxU = 0.0;
    qreal minV = 0.0;
    qreal maxV = 0.0;

    for (const PDFOCRQuad& quad : quads)
    {
        if (!quad.isValid())
        {
            continue;
        }

        for (const QPointF& point : quad.points)
        {
            const qreal u = QPointF::dotProduct(point, unitDirection);
            const qreal v = QPointF::dotProduct(point, unitNormal);

            if (!hasPoint)
            {
                minU = maxU = u;
                minV = maxV = v;
                hasPoint = true;
            }
            else
            {
                minU = qMin(minU, u);
                maxU = qMax(maxU, u);
                minV = qMin(minV, v);
                maxV = qMax(maxV, v);
            }
        }
    }

    PDFOCRQuad result;
    if (hasPoint)
    {
        result.points[0] = unitDirection * minU + unitNormal * minV;
        result.points[1] = unitDirection * maxU + unitNormal * minV;
        result.points[2] = unitDirection * maxU + unitNormal * maxV;
        result.points[3] = unitDirection * minU + unitNormal * maxV;
    }
    return result;
}

QPointF PDFOCRQuad::direction() const
{
    QLineF line(points[0], points[1]);
    const qreal length = line.length();
    if (qFuzzyIsNull(length))
    {
        return QPointF(1.0, 0.0);
    }
    return QPointF(line.dx() / length, line.dy() / length);
}

qreal PDFOCRQuad::angle() const
{
    const QPointF d = direction();
    return qRadiansToDegrees(std::atan2(d.y(), d.x()));
}

PDFOCRQuad PDFOCRQuad::translated(const QPointF& offset) const
{
    PDFOCRQuad quad = *this;
    for (QPointF& point : quad.points)
    {
        point += offset;
    }
    return quad;
}

// -------------------------------------------------------------------------
// PDFOCRWord / PDFOCRLine / PDFOCRBlock
// -------------------------------------------------------------------------

bool PDFOCRWord::isUsable() const
{
    return reviewState != PDFOCRReviewState::Discarded && !text.trimmed().isEmpty() && quad.isValid();
}

QString PDFOCRLine::getText() const
{
    QStringList texts;
    for (const PDFOCRWord& word : words)
    {
        if (word.reviewState == PDFOCRReviewState::Discarded)
        {
            continue;
        }

        texts << word.text;
    }
    return texts.join(QChar(' '));
}

void PDFOCRLine::updateGeometryFromWords()
{
    // Orientation of the line must be kept (rotated pages, rotated text): the
    // writing direction is taken from the line, or from the first valid word.
    std::vector<PDFOCRQuad> quads;
    quads.reserve(words.size());
    std::optional<QPointF> writingDirection;
    if (quad.isValid())
    {
        writingDirection = quad.direction();
    }

    for (const PDFOCRWord& word : words)
    {
        if (word.quad.isValid())
        {
            quads.push_back(word.quad);
            if (!writingDirection)
            {
                writingDirection = word.quad.direction();
            }
        }
    }

    const PDFOCRQuad united = PDFOCRQuad::fromOrientedBounds(writingDirection.value_or(QPointF(1.0, 0.0)), quads);
    if (united.isValid())
    {
        quad = united;
    }

    if (!words.empty())
    {
        // Words are stored in the logical order, quads are oriented visually
        const bool rightToLeft = direction == PDFOCRTextDirection::RightToLeft;
        const PDFOCRWord& visuallyFirst = rightToLeft ? words.back() : words.front();
        const PDFOCRWord& visuallyLast = rightToLeft ? words.front() : words.back();
        baseline = QLineF(visuallyFirst.quad.points[0], visuallyLast.quad.points[1]);
    }
}

QString PDFOCRBlock::getText() const
{
    QStringList texts;
    for (const PDFOCRLine& line : lines)
    {
        texts << line.getText();
    }
    return texts.join(QChar('\n'));
}

void PDFOCRBlock::updateGeometryFromLines()
{
    std::vector<PDFOCRQuad> quads;
    quads.reserve(lines.size());
    std::optional<QPointF> writingDirection;
    if (quad.isValid())
    {
        writingDirection = quad.direction();
    }

    for (const PDFOCRLine& line : lines)
    {
        if (line.quad.isValid())
        {
            quads.push_back(line.quad);
            if (!writingDirection)
            {
                writingDirection = line.quad.direction();
            }
        }
    }

    const PDFOCRQuad united = PDFOCRQuad::fromOrientedBounds(writingDirection.value_or(QPointF(1.0, 0.0)), quads);
    if (united.isValid())
    {
        quad = united;
    }
}

// -------------------------------------------------------------------------
// PDFOCRError
// -------------------------------------------------------------------------

PDFOCRError PDFOCRError::create(PDFOCRErrorCode code, QString message, QString step, QString detail)
{
    PDFOCRError error;
    error.code = code;
    error.message = std::move(message);
    error.step = std::move(step);
    error.detail = std::move(detail);
    return error;
}

QString PDFOCRError::getCodeIdentifier(PDFOCRErrorCode code)
{
    switch (code)
    {
        case PDFOCRErrorCode::None:
            return QStringLiteral("none");
        case PDFOCRErrorCode::MissingModel:
            return QStringLiteral("missing-model");
        case PDFOCRErrorCode::IncompatibleModel:
            return QStringLiteral("incompatible-model");
        case PDFOCRErrorCode::InitializationFailed:
            return QStringLiteral("initialization-failed");
        case PDFOCRErrorCode::InvalidConfiguration:
            return QStringLiteral("invalid-configuration");
        case PDFOCRErrorCode::InsufficientPermissions:
            return QStringLiteral("insufficient-permissions");
        case PDFOCRErrorCode::RasterizationFailed:
            return QStringLiteral("rasterization-failed");
        case PDFOCRErrorCode::ImageTooLarge:
            return QStringLiteral("image-too-large");
        case PDFOCRErrorCode::OutOfMemory:
            return QStringLiteral("out-of-memory");
        case PDFOCRErrorCode::OutOfDiskSpace:
            return QStringLiteral("out-of-disk-space");
        case PDFOCRErrorCode::Timeout:
            return QStringLiteral("timeout");
        case PDFOCRErrorCode::Cancelled:
            return QStringLiteral("cancelled");
        case PDFOCRErrorCode::WorkerCrashed:
            return QStringLiteral("worker-crashed");
        case PDFOCRErrorCode::InvalidEngineOutput:
            return QStringLiteral("invalid-engine-output");
        case PDFOCRErrorCode::IrreversibleTransformation:
            return QStringLiteral("irreversible-transformation");
        case PDFOCRErrorCode::DocumentRevisionConflict:
            return QStringLiteral("document-revision-conflict");
        case PDFOCRErrorCode::WriteFailed:
            return QStringLiteral("write-failed");
        case PDFOCRErrorCode::ExportFailed:
            return QStringLiteral("export-failed");
        case PDFOCRErrorCode::NetworkError:
            return QStringLiteral("network-error");
        case PDFOCRErrorCode::VerificationFailed:
            return QStringLiteral("verification-failed");
        case PDFOCRErrorCode::Unknown:
            break;
    }

    return QStringLiteral("unknown");
}

PDFOCRErrorCode PDFOCRError::parseCodeIdentifier(const QString& identifier)
{
    static const PDFOCRErrorCode codes[] =
    {
        PDFOCRErrorCode::None,
        PDFOCRErrorCode::MissingModel,
        PDFOCRErrorCode::IncompatibleModel,
        PDFOCRErrorCode::InitializationFailed,
        PDFOCRErrorCode::InvalidConfiguration,
        PDFOCRErrorCode::InsufficientPermissions,
        PDFOCRErrorCode::RasterizationFailed,
        PDFOCRErrorCode::ImageTooLarge,
        PDFOCRErrorCode::OutOfMemory,
        PDFOCRErrorCode::OutOfDiskSpace,
        PDFOCRErrorCode::Timeout,
        PDFOCRErrorCode::Cancelled,
        PDFOCRErrorCode::WorkerCrashed,
        PDFOCRErrorCode::InvalidEngineOutput,
        PDFOCRErrorCode::IrreversibleTransformation,
        PDFOCRErrorCode::DocumentRevisionConflict,
        PDFOCRErrorCode::WriteFailed,
        PDFOCRErrorCode::ExportFailed,
        PDFOCRErrorCode::NetworkError,
        PDFOCRErrorCode::VerificationFailed
    };

    for (PDFOCRErrorCode code : codes)
    {
        if (getCodeIdentifier(code) == identifier)
        {
            return code;
        }
    }

    return PDFOCRErrorCode::Unknown;
}

// -------------------------------------------------------------------------
// PDFOCRPageGeometry
// -------------------------------------------------------------------------

QTransform PDFOCRPageGeometry::getEngineToPage() const
{
    return getPageToEngine().inverted();
}

bool PDFOCRPageGeometry::isInvertible() const
{
    return pageToRaster.isInvertible() && rasterToEngine.isInvertible();
}

// -------------------------------------------------------------------------
// PDFOCRPageResult
// -------------------------------------------------------------------------

int PDFOCRPageResult::getWordCount() const
{
    int count = 0;
    for (const PDFOCRBlock& block : blocks)
    {
        for (const PDFOCRLine& line : block.lines)
        {
            for (const PDFOCRWord& word : line.words)
            {
                if (word.reviewState != PDFOCRReviewState::Discarded)
                {
                    ++count;
                }
            }
        }
    }
    return count;
}

std::vector<const PDFOCRWord*> PDFOCRPageResult::getWords() const
{
    std::vector<const PDFOCRWord*> words;
    for (const PDFOCRBlock& block : blocks)
    {
        for (const PDFOCRLine& line : block.lines)
        {
            for (const PDFOCRWord& word : line.words)
            {
                words.push_back(&word);
            }
        }
    }
    return words;
}

std::vector<PDFOCRWord*> PDFOCRPageResult::getWords()
{
    std::vector<PDFOCRWord*> words;
    for (PDFOCRBlock& block : blocks)
    {
        for (PDFOCRLine& line : block.lines)
        {
            for (PDFOCRWord& word : line.words)
            {
                words.push_back(&word);
            }
        }
    }
    return words;
}

const PDFOCRWord* PDFOCRPageResult::findWord(int id) const
{
    for (const PDFOCRBlock& block : blocks)
    {
        for (const PDFOCRLine& line : block.lines)
        {
            for (const PDFOCRWord& word : line.words)
            {
                if (word.id == id)
                {
                    return &word;
                }
            }
        }
    }
    return nullptr;
}

PDFOCRWord* PDFOCRPageResult::findWord(int id)
{
    return const_cast<PDFOCRWord*>(std::as_const(*this).findWord(id));
}

const PDFOCRWord* PDFOCRPageResult::findOriginalWord(int id) const
{
    for (const PDFOCRBlock& block : originalBlocks)
    {
        for (const PDFOCRLine& line : block.lines)
        {
            for (const PDFOCRWord& word : line.words)
            {
                if (word.id == id)
                {
                    return &word;
                }
            }
        }
    }
    return nullptr;
}

std::vector<const PDFOCRWord*> PDFOCRPageResult::getOriginalWords() const
{
    std::vector<const PDFOCRWord*> words;
    for (const PDFOCRBlock& block : originalBlocks)
    {
        for (const PDFOCRLine& line : block.lines)
        {
            for (const PDFOCRWord& word : line.words)
            {
                words.push_back(&word);
            }
        }
    }
    return words;
}

const PDFOCRLine* PDFOCRPageResult::findLine(int id) const
{
    for (const PDFOCRBlock& block : blocks)
    {
        for (const PDFOCRLine& line : block.lines)
        {
            if (line.id == id)
            {
                return &line;
            }
        }
    }
    return nullptr;
}

PDFOCRLine* PDFOCRPageResult::findLine(int id)
{
    return const_cast<PDFOCRLine*>(std::as_const(*this).findLine(id));
}

const PDFOCRLine* PDFOCRPageResult::findLineOfWord(int wordId) const
{
    for (const PDFOCRBlock& block : blocks)
    {
        for (const PDFOCRLine& line : block.lines)
        {
            for (const PDFOCRWord& word : line.words)
            {
                if (word.id == wordId)
                {
                    return &line;
                }
            }
        }
    }
    return nullptr;
}

PDFOCRLine* PDFOCRPageResult::findLineOfWord(int wordId)
{
    return const_cast<PDFOCRLine*>(std::as_const(*this).findLineOfWord(wordId));
}

const PDFOCRBlock* PDFOCRPageResult::findBlock(int id) const
{
    for (const PDFOCRBlock& block : blocks)
    {
        if (block.id == id)
        {
            return &block;
        }
    }
    return nullptr;
}

PDFOCRBlock* PDFOCRPageResult::findBlock(int id)
{
    return const_cast<PDFOCRBlock*>(std::as_const(*this).findBlock(id));
}

PDFOCRBlock* PDFOCRPageResult::findBlockOfLine(int lineId)
{
    for (PDFOCRBlock& block : blocks)
    {
        for (const PDFOCRLine& line : block.lines)
        {
            if (line.id == lineId)
            {
                return &block;
            }
        }
    }
    return nullptr;
}

const PDFOCRRegion* PDFOCRPageResult::findRegion(int id) const
{
    for (const PDFOCRRegion& region : regions)
    {
        if (region.id == id)
        {
            return &region;
        }
    }
    return nullptr;
}

PDFOCRRegion* PDFOCRPageResult::findRegion(int id)
{
    return const_cast<PDFOCRRegion*>(std::as_const(*this).findRegion(id));
}

QString PDFOCRPageResult::getText() const
{
    QStringList texts;
    for (const PDFOCRBlock& block : blocks)
    {
        const QString text = block.getText();
        if (!text.trimmed().isEmpty())
        {
            texts << text;
        }
    }
    return texts.join(QStringLiteral("\n\n"));
}

void PDFOCRPageResult::assignIdentifiers()
{
    QSet<int> used;

    auto assign = [&](int& id)
    {
        if (id <= 0 || used.contains(id))
        {
            while (used.contains(nextId))
            {
                ++nextId;
            }
            id = nextId++;
        }
        used.insert(id);
        nextId = qMax(nextId, id + 1);
    };

    for (PDFOCRRegion& region : regions)
    {
        assign(region.id);
    }

    for (PDFOCRBlock& block : blocks)
    {
        assign(block.id);
        for (PDFOCRLine& line : block.lines)
        {
            assign(line.id);
            for (PDFOCRWord& word : line.words)
            {
                assign(word.id);
            }
        }
    }
}

bool PDFOCRPageResult::hasUsableText() const
{
    for (const PDFOCRBlock& block : blocks)
    {
        for (const PDFOCRLine& line : block.lines)
        {
            for (const PDFOCRWord& word : line.words)
            {
                if (word.isUsable())
                {
                    return true;
                }
            }
        }
    }
    return false;
}

// -------------------------------------------------------------------------
// PDFOCRConfidenceStatistics
// -------------------------------------------------------------------------

PDFOCRConfidenceStatistics PDFOCRConfidenceStatistics::compute(const PDFOCRPageResult& page, double threshold)
{
    PDFOCRConfidenceStatistics statistics;

    for (const PDFOCRWord* word : page.getWords())
    {
        switch (word->reviewState)
        {
            case PDFOCRReviewState::Unreviewed:
                ++statistics.unreviewedCount;
                break;
            case PDFOCRReviewState::Confirmed:
                ++statistics.confirmedCount;
                break;
            case PDFOCRReviewState::Modified:
                ++statistics.modifiedCount;
                break;
            case PDFOCRReviewState::Discarded:
                ++statistics.discardedCount;
                continue;
        }

        ++statistics.wordCount;

        if (word->textOrigin == PDFOCRTextOrigin::Manual)
        {
            ++statistics.manualWordCount;
        }

        if (word->confidence.isAvailable())
        {
            ++statistics.scoredWordCount;
            statistics.m_scoreSum += word->confidence.normalized.value();

            if (statistics.level == PDFOCRConfidenceLevel::Unknown)
            {
                statistics.level = word->confidence.level;
            }

            if (PDFOCRReview::isBelowThreshold(word->confidence, threshold))
            {
                ++statistics.belowThresholdCount;
            }
        }
        else
        {
            ++statistics.unknownWordCount;
        }

        if (PDFOCRReview::requiresReview(*word, threshold))
        {
            ++statistics.reviewRequiredCount;
        }
    }

    if (statistics.scoredWordCount > 0)
    {
        statistics.meanScore = statistics.m_scoreSum / statistics.scoredWordCount;
    }

    return statistics;
}

void PDFOCRConfidenceStatistics::merge(const PDFOCRConfidenceStatistics& other)
{
    wordCount += other.wordCount;
    scoredWordCount += other.scoredWordCount;
    unknownWordCount += other.unknownWordCount;
    manualWordCount += other.manualWordCount;
    belowThresholdCount += other.belowThresholdCount;
    unreviewedCount += other.unreviewedCount;
    modifiedCount += other.modifiedCount;
    confirmedCount += other.confirmedCount;
    discardedCount += other.discardedCount;
    reviewRequiredCount += other.reviewRequiredCount;
    m_scoreSum += other.m_scoreSum;

    if (level == PDFOCRConfidenceLevel::Unknown)
    {
        level = other.level;
    }

    if (scoredWordCount > 0)
    {
        meanScore = m_scoreSum / scoredWordCount;
    }
    else
    {
        meanScore.reset();
    }
}

// -------------------------------------------------------------------------
// PDFOCRReview
// -------------------------------------------------------------------------

bool PDFOCRReview::requiresReview(const PDFOCRWord& word, double threshold)
{
    switch (word.reviewState)
    {
        case PDFOCRReviewState::Confirmed:
        case PDFOCRReviewState::Discarded:
            return false;

        case PDFOCRReviewState::Modified:
            // Manual changes which were not confirmed are highlighted (CONF-03)
            return true;

        case PDFOCRReviewState::Unreviewed:
            break;
    }

    if (word.overlapsExcludedRegion || word.hasExtremeScaling)
    {
        return true;
    }

    if (word.textOrigin == PDFOCRTextOrigin::Manual)
    {
        return true;
    }

    if (!word.confidence.isAvailable())
    {
        return true;
    }

    return isBelowThreshold(word.confidence, threshold);
}

bool PDFOCRReview::isBelowThreshold(const PDFOCRConfidence& confidence, double threshold)
{
    return confidence.isAvailable() && confidence.normalized.value() < threshold;
}

// -------------------------------------------------------------------------
// PDFOCRValidator
// -------------------------------------------------------------------------

QStringList PDFOCRValidator::validate(const PDFOCRPageResult& page, const Limits& limits)
{
    QStringList errors;

    QSet<int> identifiers;
    auto checkIdentifier = [&](int id, const char* what)
    {
        if (id <= 0)
        {
            errors << QStringLiteral("%1: invalid identifier %2").arg(QLatin1String(what)).arg(id);
        }
        else if (identifiers.contains(id))
        {
            errors << QStringLiteral("%1: duplicate identifier %2").arg(QLatin1String(what)).arg(id);
        }
        identifiers.insert(id);
    };

    auto checkQuad = [&](const PDFOCRQuad& quad, int id, const char* what)
    {
        for (const QPointF& point : quad.points)
        {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
                std::abs(point.x()) > limits.coordinateLimit || std::abs(point.y()) > limits.coordinateLimit)
            {
                errors << QStringLiteral("%1 %2: coordinates out of range").arg(QLatin1String(what)).arg(id);
                return;
            }
        }
    };

    if (page.pageIndex < 0)
    {
        errors << QStringLiteral("Invalid page index");
    }

    for (const PDFOCRRegion& region : page.regions)
    {
        checkIdentifier(region.id, "Region");
        if (!isValidRect(region.rect, limits.coordinateLimit))
        {
            errors << QStringLiteral("Region %1: invalid rectangle").arg(region.id);
        }
    }

    if (int(page.regions.size()) > limits.maximumRegionsPerPage)
    {
        errors << QStringLiteral("Too many regions on the page (%1, at most %2 are allowed)").arg(page.regions.size()).arg(limits.maximumRegionsPerPage);
    }

    int wordCount = 0;
    for (const PDFOCRBlock& block : page.blocks)
    {
        checkIdentifier(block.id, "Block");
        if (block.regionId != -1 && !page.findRegion(block.regionId))
        {
            errors << QStringLiteral("Block %1: unknown region %2").arg(block.id).arg(block.regionId);
        }

        for (const PDFOCRLine& line : block.lines)
        {
            checkIdentifier(line.id, "Line");

            for (const PDFOCRWord& word : line.words)
            {
                ++wordCount;
                checkIdentifier(word.id, "Word");

                if (word.reviewState == PDFOCRReviewState::Discarded)
                {
                    continue;
                }

                if (!isValidText(word.text))
                {
                    errors << QStringLiteral("Word %1: invalid text").arg(word.id);
                }

                if (word.text.length() > limits.maximumTextLength)
                {
                    errors << QStringLiteral("Word %1: text too long (%2 characters, at most %3 are allowed)").arg(word.id).arg(word.text.length()).arg(limits.maximumTextLength);
                }

                if (!word.text.trimmed().isEmpty())
                {
                    checkQuad(word.quad, word.id, "Word");
                    if (!word.quad.isValid())
                    {
                        errors << QStringLiteral("Word %1: invalid geometry").arg(word.id);
                    }
                }
            }
        }
    }

    if (wordCount > limits.maximumWordsPerPage)
    {
        errors << QStringLiteral("Too many words on the page (%1, at most %2 are allowed)").arg(wordCount).arg(limits.maximumWordsPerPage);
    }

    return errors;
}

bool PDFOCRValidator::isValidText(const QString& text)
{
    for (int i = 0; i < text.size(); ++i)
    {
        const QChar character = text[i];

        if (character.isNull())
        {
            return false;
        }

        if (character.isHighSurrogate())
        {
            if (i + 1 >= text.size() || !text[i + 1].isLowSurrogate())
            {
                return false;
            }
            ++i;
            continue;
        }

        if (character.isLowSurrogate())
        {
            return false;
        }
    }

    return true;
}

bool PDFOCRValidator::isValidRect(const QRectF& rect, double coordinateLimit)
{
    const qreal values[] = { rect.left(), rect.top(), rect.right(), rect.bottom() };
    for (qreal value : values)
    {
        if (!std::isfinite(value) || std::abs(value) > coordinateLimit)
        {
            return false;
        }
    }

    return rect.width() > 0.0 && rect.height() > 0.0;
}

// -------------------------------------------------------------------------
// PDFOCREnumerations
// -------------------------------------------------------------------------

template<typename Enum>
struct PDFOCREnumerationEntry
{
    Enum value;
    const char* name;
};

template<typename Enum, size_t N>
static QString enumToString(const PDFOCREnumerationEntry<Enum> (&entries)[N], Enum value)
{
    for (const auto& entry : entries)
    {
        if (entry.value == value)
        {
            return QLatin1String(entry.name);
        }
    }
    return QString();
}

template<typename Enum, size_t N>
static Enum stringToEnum(const PDFOCREnumerationEntry<Enum> (&entries)[N], const QString& value)
{
    for (const auto& entry : entries)
    {
        if (value == QLatin1String(entry.name))
        {
            return entry.value;
        }
    }
    return entries[0].value;
}

static const PDFOCREnumerationEntry<PDFOCRPageState> s_pageStates[] =
{
    { PDFOCRPageState::Pending, "pending" },
    { PDFOCRPageState::Preparing, "preparing" },
    { PDFOCRPageState::Recognizing, "recognizing" },
    { PDFOCRPageState::Done, "done" },
    { PDFOCRPageState::NoText, "no-text" },
    { PDFOCRPageState::Skipped, "skipped" },
    { PDFOCRPageState::Error, "error" },
    { PDFOCRPageState::Cancelled, "cancelled" },
    { PDFOCRPageState::Stale, "stale" }
};

static const PDFOCREnumerationEntry<PDFOCRReviewState> s_reviewStates[] =
{
    { PDFOCRReviewState::Unreviewed, "unreviewed" },
    { PDFOCRReviewState::Confirmed, "confirmed" },
    { PDFOCRReviewState::Modified, "modified" },
    { PDFOCRReviewState::Discarded, "discarded" }
};

static const PDFOCREnumerationEntry<PDFOCRGeometryOrigin> s_geometryOrigins[] =
{
    { PDFOCRGeometryOrigin::Engine, "engine" },
    { PDFOCRGeometryOrigin::Estimated, "estimated" },
    { PDFOCRGeometryOrigin::Manual, "manual" },
    { PDFOCRGeometryOrigin::Imported, "imported" },
    { PDFOCRGeometryOrigin::Digital, "digital" }
};

static const PDFOCREnumerationEntry<PDFOCRTextOrigin> s_textOrigins[] =
{
    { PDFOCRTextOrigin::OCR, "ocr" },
    { PDFOCRTextOrigin::Manual, "manual" },
    { PDFOCRTextOrigin::Imported, "imported" },
    { PDFOCRTextOrigin::Digital, "digital" }
};

static const PDFOCREnumerationEntry<PDFOCRConfidenceLevel> s_confidenceLevels[] =
{
    { PDFOCRConfidenceLevel::Unknown, "unknown" },
    { PDFOCRConfidenceLevel::Symbol, "symbol" },
    { PDFOCRConfidenceLevel::Word, "word" },
    { PDFOCRConfidenceLevel::Line, "line" },
    { PDFOCRConfidenceLevel::Block, "block" },
    { PDFOCRConfidenceLevel::Page, "page" }
};

static const PDFOCREnumerationEntry<PDFOCRBlockType> s_blockTypes[] =
{
    { PDFOCRBlockType::Text, "text" },
    { PDFOCRBlockType::Table, "table" },
    { PDFOCRBlockType::Image, "image" },
    { PDFOCRBlockType::Separator, "separator" },
    { PDFOCRBlockType::Other, "other" }
};

static const PDFOCREnumerationEntry<PDFOCRRegionType> s_regionTypes[] =
{
    { PDFOCRRegionType::Recognize, "recognize" },
    { PDFOCRRegionType::Exclude, "exclude" }
};

static const PDFOCREnumerationEntry<PDFOCRTextDirection> s_textDirections[] =
{
    { PDFOCRTextDirection::LeftToRight, "ltr" },
    { PDFOCRTextDirection::RightToLeft, "rtl" },
    { PDFOCRTextDirection::TopToBottom, "ttb" }
};

static const PDFOCREnumerationEntry<PDFOCRPageContentClass> s_contentClasses[] =
{
    { PDFOCRPageContentClass::Unknown, "unknown" },
    { PDFOCRPageContentClass::Image, "image" },
    { PDFOCRPageContentClass::VisibleText, "visible-text" },
    { PDFOCRPageContentClass::InvisibleText, "invisible-text" },
    { PDFOCRPageContentClass::Mixed, "mixed" },
    { PDFOCRPageContentClass::Empty, "empty" },
    { PDFOCRPageContentClass::Ambiguous, "ambiguous" }
};

QString PDFOCREnumerations::toString(PDFOCRPageState value) { return enumToString(s_pageStates, value); }
QString PDFOCREnumerations::toString(PDFOCRReviewState value) { return enumToString(s_reviewStates, value); }
QString PDFOCREnumerations::toString(PDFOCRGeometryOrigin value) { return enumToString(s_geometryOrigins, value); }
QString PDFOCREnumerations::toString(PDFOCRTextOrigin value) { return enumToString(s_textOrigins, value); }
QString PDFOCREnumerations::toString(PDFOCRConfidenceLevel value) { return enumToString(s_confidenceLevels, value); }
QString PDFOCREnumerations::toString(PDFOCRBlockType value) { return enumToString(s_blockTypes, value); }
QString PDFOCREnumerations::toString(PDFOCRRegionType value) { return enumToString(s_regionTypes, value); }
QString PDFOCREnumerations::toString(PDFOCRTextDirection value) { return enumToString(s_textDirections, value); }
QString PDFOCREnumerations::toString(PDFOCRPageContentClass value) { return enumToString(s_contentClasses, value); }

PDFOCRPageState PDFOCREnumerations::toPageState(const QString& value) { return stringToEnum(s_pageStates, value); }
PDFOCRReviewState PDFOCREnumerations::toReviewState(const QString& value) { return stringToEnum(s_reviewStates, value); }
PDFOCRGeometryOrigin PDFOCREnumerations::toGeometryOrigin(const QString& value) { return stringToEnum(s_geometryOrigins, value); }
PDFOCRTextOrigin PDFOCREnumerations::toTextOrigin(const QString& value) { return stringToEnum(s_textOrigins, value); }
PDFOCRConfidenceLevel PDFOCREnumerations::toConfidenceLevel(const QString& value) { return stringToEnum(s_confidenceLevels, value); }
PDFOCRBlockType PDFOCREnumerations::toBlockType(const QString& value) { return stringToEnum(s_blockTypes, value); }
PDFOCRRegionType PDFOCREnumerations::toRegionType(const QString& value) { return stringToEnum(s_regionTypes, value); }
PDFOCRTextDirection PDFOCREnumerations::toTextDirection(const QString& value) { return stringToEnum(s_textDirections, value); }
PDFOCRPageContentClass PDFOCREnumerations::toPageContentClass(const QString& value) { return stringToEnum(s_contentClasses, value); }

}   // namespace pdf
