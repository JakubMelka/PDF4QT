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

#include "pdfocrproject.h"
#include "pdfconstants.h"

#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace pdf
{

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

static QJsonArray toJsonArray(const QStringList& list)
{
    QJsonArray array;
    for (const QString& item : list)
    {
        array.append(item);
    }
    return array;
}

static QStringList toStringList(const QJsonValue& value)
{
    QStringList list;
    for (const QJsonValue& item : value.toArray())
    {
        list << item.toString();
    }
    return list;
}

static QJsonArray toJsonArray(const std::vector<int>& list)
{
    QJsonArray array;
    for (int item : list)
    {
        array.append(item);
    }
    return array;
}

static std::vector<int> toIntVector(const QJsonValue& value)
{
    std::vector<int> list;
    for (const QJsonValue& item : value.toArray())
    {
        list.push_back(item.toInt());
    }
    return list;
}

static QJsonArray toJsonArray(const std::vector<PDFInteger>& list)
{
    QJsonArray array;
    for (PDFInteger item : list)
    {
        array.append(qint64(item));
    }
    return array;
}

static std::vector<PDFInteger> toIntegerVector(const QJsonValue& value)
{
    std::vector<PDFInteger> list;
    for (const QJsonValue& item : value.toArray())
    {
        list.push_back(PDFInteger(item.toDouble()));
    }
    return list;
}

static QJsonArray rectToJson(const QRectF& rect)
{
    QJsonArray array;
    array.append(rect.left());
    array.append(rect.top());
    array.append(rect.width());
    array.append(rect.height());
    return array;
}

static QRectF rectFromJson(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() >= 4)
    {
        return QRectF(array[0].toDouble(), array[1].toDouble(), array[2].toDouble(), array[3].toDouble());
    }
    return QRectF();
}

static QJsonArray lineToJsonArray(const QLineF& line)
{
    QJsonArray array;
    array.append(line.x1());
    array.append(line.y1());
    array.append(line.x2());
    array.append(line.y2());
    return array;
}

static QLineF lineFromJsonArray(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() >= 4)
    {
        return QLineF(array[0].toDouble(), array[1].toDouble(), array[2].toDouble(), array[3].toDouble());
    }
    return QLineF();
}

static QJsonArray transformToJson(const QTransform& transform)
{
    QJsonArray array;
    array.append(transform.m11());
    array.append(transform.m12());
    array.append(transform.m21());
    array.append(transform.m22());
    array.append(transform.dx());
    array.append(transform.dy());
    return array;
}

static QTransform transformFromJson(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() >= 6)
    {
        return QTransform(array[0].toDouble(), array[1].toDouble(), array[2].toDouble(), array[3].toDouble(), array[4].toDouble(), array[5].toDouble());
    }
    return QTransform();
}

static QJsonArray sizeToJson(const QSize& size)
{
    QJsonArray array;
    array.append(size.width());
    array.append(size.height());
    return array;
}

static QSize sizeFromJson(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() >= 2)
    {
        return QSize(array[0].toInt(), array[1].toInt());
    }
    return QSize();
}

static QString dateTimeToJson(const QDateTime& dateTime)
{
    return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODateWithMs) : QString();
}

static QDateTime dateTimeFromJson(const QJsonValue& value)
{
    const QString text = value.toString();
    return text.isEmpty() ? QDateTime() : QDateTime::fromString(text, Qt::ISODateWithMs);
}

// -------------------------------------------------------------------------
// PDFOCRDocumentIdentity
// -------------------------------------------------------------------------

QJsonObject PDFOCRDocumentIdentity::toJson() const
{
    QJsonObject object;
    object[QStringLiteral("fileName")] = fileName;
    object[QStringLiteral("sourceHash")] = QString::fromLatin1(sourceHash.toHex());
    object[QStringLiteral("fingerprint")] = QString::fromLatin1(fingerprint.toHex());
    object[QStringLiteral("pageCount")] = qint64(pageCount);
    object[QStringLiteral("isEncrypted")] = isEncrypted;
    return object;
}

PDFOCRDocumentIdentity PDFOCRDocumentIdentity::fromJson(const QJsonObject& object)
{
    PDFOCRDocumentIdentity identity;
    identity.fileName = object.value(QStringLiteral("fileName")).toString();
    identity.sourceHash = QByteArray::fromHex(object.value(QStringLiteral("sourceHash")).toString().toLatin1());
    identity.fingerprint = QByteArray::fromHex(object.value(QStringLiteral("fingerprint")).toString().toLatin1());
    identity.pageCount = PDFInteger(object.value(QStringLiteral("pageCount")).toDouble());
    identity.isEncrypted = object.value(QStringLiteral("isEncrypted")).toBool();
    return identity;
}

// -------------------------------------------------------------------------
// PDFOCRProjectSerializer
// -------------------------------------------------------------------------

QJsonObject PDFOCRProjectSerializer::quadToJson(const PDFOCRQuad& quad)
{
    QJsonObject object;
    QJsonArray points;
    for (const QPointF& point : quad.points)
    {
        points.append(point.x());
        points.append(point.y());
    }
    object[QStringLiteral("p")] = points;
    return object;
}

PDFOCRQuad PDFOCRProjectSerializer::quadFromJson(const QJsonValue& value)
{
    PDFOCRQuad quad;
    const QJsonArray points = value.toObject().value(QStringLiteral("p")).toArray();
    if (points.size() >= 8)
    {
        for (size_t i = 0; i < 4; ++i)
        {
            quad.points[i] = QPointF(points[int(i * 2)].toDouble(), points[int(i * 2 + 1)].toDouble());
        }
    }
    return quad;
}

QJsonObject PDFOCRProjectSerializer::confidenceToJson(const PDFOCRConfidence& confidence)
{
    QJsonObject object;
    if (confidence.normalized)
    {
        object[QStringLiteral("normalized")] = *confidence.normalized;
    }
    if (confidence.raw)
    {
        object[QStringLiteral("raw")] = *confidence.raw;
    }
    object[QStringLiteral("rawMinimum")] = confidence.rawMinimum;
    object[QStringLiteral("rawMaximum")] = confidence.rawMaximum;
    object[QStringLiteral("level")] = PDFOCREnumerations::toString(confidence.level);
    return object;
}

PDFOCRConfidence PDFOCRProjectSerializer::confidenceFromJson(const QJsonValue& value)
{
    PDFOCRConfidence confidence;
    const QJsonObject object = value.toObject();
    if (object.contains(QStringLiteral("normalized")))
    {
        confidence.normalized = object.value(QStringLiteral("normalized")).toDouble();
    }
    if (object.contains(QStringLiteral("raw")))
    {
        confidence.raw = object.value(QStringLiteral("raw")).toDouble();
    }
    confidence.rawMinimum = object.value(QStringLiteral("rawMinimum")).toDouble(0.0);
    confidence.rawMaximum = object.value(QStringLiteral("rawMaximum")).toDouble(100.0);
    confidence.level = PDFOCREnumerations::toConfidenceLevel(object.value(QStringLiteral("level")).toString());
    return confidence;
}

QJsonObject PDFOCRProjectSerializer::wordToJson(const PDFOCRWord& word, PDFOCRSerializationFlags flags)
{
    QJsonObject object;
    object[QStringLiteral("id")] = word.id;
    object[QStringLiteral("text")] = word.text;
    object[QStringLiteral("quad")] = quadToJson(word.quad);
    object[QStringLiteral("geometryOrigin")] = PDFOCREnumerations::toString(word.geometryOrigin);
    object[QStringLiteral("textOrigin")] = PDFOCREnumerations::toString(word.textOrigin);
    if (!word.language.isEmpty())
    {
        object[QStringLiteral("language")] = word.language;
    }

    if (flags.testFlag(PDFOCRSerializationFlag::ReviewData))
    {
        object[QStringLiteral("originalText")] = word.originalText;
        object[QStringLiteral("confidence")] = confidenceToJson(word.confidence);
        object[QStringLiteral("reviewState")] = PDFOCREnumerations::toString(word.reviewState);
        if (word.reviewTime)
        {
            object[QStringLiteral("reviewTime")] = dateTimeToJson(*word.reviewTime);
        }
        if (!word.predecessorIds.empty())
        {
            object[QStringLiteral("predecessors")] = toJsonArray(word.predecessorIds);
        }
        object[QStringLiteral("overlapsExcludedRegion")] = word.overlapsExcludedRegion;
        object[QStringLiteral("hasExtremeScaling")] = word.hasExtremeScaling;
        if (word.inDictionary.has_value())
        {
            object[QStringLiteral("inDictionary")] = *word.inDictionary;
        }
    }
    else if (word.reviewState == PDFOCRReviewState::Discarded)
    {
        object[QStringLiteral("reviewState")] = PDFOCREnumerations::toString(word.reviewState);
    }

    return object;
}

PDFOCRWord PDFOCRProjectSerializer::wordFromJson(const QJsonObject& object)
{
    PDFOCRWord word;
    word.id = object.value(QStringLiteral("id")).toInt();
    word.text = object.value(QStringLiteral("text")).toString();
    word.originalText = object.contains(QStringLiteral("originalText")) ? object.value(QStringLiteral("originalText")).toString() : word.text;
    word.quad = quadFromJson(object.value(QStringLiteral("quad")));
    word.geometryOrigin = PDFOCREnumerations::toGeometryOrigin(object.value(QStringLiteral("geometryOrigin")).toString());
    word.textOrigin = PDFOCREnumerations::toTextOrigin(object.value(QStringLiteral("textOrigin")).toString());
    word.language = object.value(QStringLiteral("language")).toString();
    if (object.contains(QStringLiteral("confidence")))
    {
        word.confidence = confidenceFromJson(object.value(QStringLiteral("confidence")));
    }
    if (object.contains(QStringLiteral("reviewState")))
    {
        word.reviewState = PDFOCREnumerations::toReviewState(object.value(QStringLiteral("reviewState")).toString());
    }
    if (object.contains(QStringLiteral("reviewTime")))
    {
        const QDateTime time = dateTimeFromJson(object.value(QStringLiteral("reviewTime")));
        if (time.isValid())
        {
            word.reviewTime = time;
        }
    }
    word.predecessorIds = toIntVector(object.value(QStringLiteral("predecessors")));
    word.overlapsExcludedRegion = object.value(QStringLiteral("overlapsExcludedRegion")).toBool();
    word.hasExtremeScaling = object.value(QStringLiteral("hasExtremeScaling")).toBool();
    const QJsonValue inDictionary = object.value(QStringLiteral("inDictionary"));
    if (inDictionary.isBool())
    {
        word.inDictionary = inDictionary.toBool();
    }
    return word;
}

QJsonObject PDFOCRProjectSerializer::lineToJson(const PDFOCRLine& line, PDFOCRSerializationFlags flags)
{
    QJsonObject object;
    object[QStringLiteral("id")] = line.id;
    object[QStringLiteral("baseline")] = lineToJsonArray(line.baseline);
    object[QStringLiteral("quad")] = quadToJson(line.quad);
    object[QStringLiteral("direction")] = PDFOCREnumerations::toString(line.direction);
    if (flags.testFlag(PDFOCRSerializationFlag::ReviewData))
    {
        object[QStringLiteral("confidence")] = confidenceToJson(line.confidence);
    }

    QJsonArray words;
    for (const PDFOCRWord& word : line.words)
    {
        words.append(wordToJson(word, flags));
    }
    object[QStringLiteral("words")] = words;
    return object;
}

PDFOCRLine PDFOCRProjectSerializer::lineFromJson(const QJsonObject& object)
{
    PDFOCRLine line;
    line.id = object.value(QStringLiteral("id")).toInt();
    line.baseline = lineFromJsonArray(object.value(QStringLiteral("baseline")));
    line.quad = quadFromJson(object.value(QStringLiteral("quad")));
    line.direction = PDFOCREnumerations::toTextDirection(object.value(QStringLiteral("direction")).toString());
    if (object.contains(QStringLiteral("confidence")))
    {
        line.confidence = confidenceFromJson(object.value(QStringLiteral("confidence")));
    }
    for (const QJsonValue& value : object.value(QStringLiteral("words")).toArray())
    {
        line.words.push_back(wordFromJson(value.toObject()));
    }
    return line;
}

QJsonObject PDFOCRProjectSerializer::blockToJson(const PDFOCRBlock& block, PDFOCRSerializationFlags flags)
{
    QJsonObject object;
    object[QStringLiteral("id")] = block.id;
    object[QStringLiteral("type")] = PDFOCREnumerations::toString(block.type);
    object[QStringLiteral("quad")] = quadToJson(block.quad);
    object[QStringLiteral("regionId")] = block.regionId;

    QJsonArray lines;
    for (const PDFOCRLine& line : block.lines)
    {
        lines.append(lineToJson(line, flags));
    }
    object[QStringLiteral("lines")] = lines;
    return object;
}

PDFOCRBlock PDFOCRProjectSerializer::blockFromJson(const QJsonObject& object)
{
    PDFOCRBlock block;
    block.id = object.value(QStringLiteral("id")).toInt();
    block.type = PDFOCREnumerations::toBlockType(object.value(QStringLiteral("type")).toString());
    block.quad = quadFromJson(object.value(QStringLiteral("quad")));
    block.regionId = object.value(QStringLiteral("regionId")).toInt(-1);
    for (const QJsonValue& value : object.value(QStringLiteral("lines")).toArray())
    {
        block.lines.push_back(lineFromJson(value.toObject()));
    }
    return block;
}

QJsonObject PDFOCRProjectSerializer::regionToJson(const PDFOCRRegion& region)
{
    QJsonObject object;
    object[QStringLiteral("id")] = region.id;
    object[QStringLiteral("type")] = PDFOCREnumerations::toString(region.type);
    object[QStringLiteral("name")] = region.name;
    object[QStringLiteral("rect")] = rectToJson(region.rect);
    object[QStringLiteral("order")] = region.order;
    object[QStringLiteral("proposedByAnalysis")] = region.proposedByAnalysis;

    QJsonObject configuration;
    if (!region.configuration.languages.isEmpty())
    {
        configuration[QStringLiteral("languages")] = toJsonArray(region.configuration.languages);
    }
    configuration[QStringLiteral("segmentation")] = region.configuration.segmentation;
    configuration[QStringLiteral("rotation")] = region.configuration.rotation;
    object[QStringLiteral("configuration")] = configuration;
    return object;
}

PDFOCRRegion PDFOCRProjectSerializer::regionFromJson(const QJsonObject& object)
{
    PDFOCRRegion region;
    region.id = object.value(QStringLiteral("id")).toInt();
    region.type = PDFOCREnumerations::toRegionType(object.value(QStringLiteral("type")).toString());
    region.name = object.value(QStringLiteral("name")).toString();
    region.rect = rectFromJson(object.value(QStringLiteral("rect")));
    region.order = object.value(QStringLiteral("order")).toInt();
    region.proposedByAnalysis = object.value(QStringLiteral("proposedByAnalysis")).toBool();

    const QJsonObject configuration = object.value(QStringLiteral("configuration")).toObject();
    region.configuration.languages = toStringList(configuration.value(QStringLiteral("languages")));
    region.configuration.segmentation = configuration.value(QStringLiteral("segmentation")).toInt(-1);
    region.configuration.rotation = configuration.value(QStringLiteral("rotation")).toInt(-1);
    return region;
}

QJsonObject PDFOCRProjectSerializer::geometryToJson(const PDFOCRPageGeometry& geometry)
{
    QJsonObject object;
    object[QStringLiteral("mediaBox")] = rectToJson(geometry.mediaBox);
    object[QStringLiteral("cropBox")] = rectToJson(geometry.cropBox);
    object[QStringLiteral("rotation")] = geometry.rotation;
    object[QStringLiteral("userUnit")] = geometry.userUnit;
    object[QStringLiteral("rasterSize")] = sizeToJson(geometry.rasterSize);
    object[QStringLiteral("dpi")] = geometry.dpi;
    object[QStringLiteral("requestedDpi")] = geometry.requestedDpi;
    object[QStringLiteral("pageToRaster")] = transformToJson(geometry.pageToRaster);
    object[QStringLiteral("rasterToEngine")] = transformToJson(geometry.rasterToEngine);
    object[QStringLiteral("engineImageSize")] = sizeToJson(geometry.engineImageSize);
    object[QStringLiteral("pipeline")] = toJsonArray(geometry.pipeline);
    return object;
}

PDFOCRPageGeometry PDFOCRProjectSerializer::geometryFromJson(const QJsonObject& object)
{
    PDFOCRPageGeometry geometry;
    geometry.mediaBox = rectFromJson(object.value(QStringLiteral("mediaBox")));
    geometry.cropBox = rectFromJson(object.value(QStringLiteral("cropBox")));
    geometry.rotation = object.value(QStringLiteral("rotation")).toInt();
    geometry.userUnit = object.value(QStringLiteral("userUnit")).toDouble(1.0);
    geometry.rasterSize = sizeFromJson(object.value(QStringLiteral("rasterSize")));
    geometry.dpi = object.value(QStringLiteral("dpi")).toDouble();
    geometry.requestedDpi = object.value(QStringLiteral("requestedDpi")).toDouble();
    geometry.pageToRaster = transformFromJson(object.value(QStringLiteral("pageToRaster")));
    geometry.rasterToEngine = transformFromJson(object.value(QStringLiteral("rasterToEngine")));
    geometry.engineImageSize = sizeFromJson(object.value(QStringLiteral("engineImageSize")));
    geometry.pipeline = toStringList(object.value(QStringLiteral("pipeline")));
    return geometry;
}

QJsonObject PDFOCRProjectSerializer::provenanceToJson(const PDFOCRProvenance& provenance)
{
    QJsonObject object;
    object[QStringLiteral("engineId")] = provenance.engineId;
    object[QStringLiteral("engineVersion")] = provenance.engineVersion;
    object[QStringLiteral("modelIds")] = toJsonArray(provenance.modelIds);
    object[QStringLiteral("modelSetHash")] = provenance.modelSetHash;
    object[QStringLiteral("parameters")] = QJsonObject::fromVariantMap(provenance.parameters);
    return object;
}

PDFOCRProvenance PDFOCRProjectSerializer::provenanceFromJson(const QJsonObject& object)
{
    PDFOCRProvenance provenance;
    provenance.engineId = object.value(QStringLiteral("engineId")).toString();
    provenance.engineVersion = object.value(QStringLiteral("engineVersion")).toString();
    provenance.modelIds = toStringList(object.value(QStringLiteral("modelIds")));
    provenance.modelSetHash = object.value(QStringLiteral("modelSetHash")).toString();
    provenance.parameters = object.value(QStringLiteral("parameters")).toObject().toVariantMap();
    return provenance;
}

QJsonObject PDFOCRProjectSerializer::analysisToJson(const PDFOCRPageAnalysis& analysis)
{
    QJsonObject object;
    object[QStringLiteral("contentClass")] = PDFOCREnumerations::toString(analysis.contentClass);
    object[QStringLiteral("hasImages")] = analysis.hasImages;
    object[QStringLiteral("hasVisibleText")] = analysis.hasVisibleText;
    object[QStringLiteral("hasInvisibleText")] = analysis.hasInvisibleText;
    object[QStringLiteral("hasOwnOCRLayer")] = analysis.hasOwnOCRLayer;
    object[QStringLiteral("hasAnnotations")] = analysis.hasAnnotations;
    object[QStringLiteral("hasFormFields")] = analysis.hasFormFields;
    object[QStringLiteral("hasUnappliedRedactions")] = analysis.hasUnappliedRedactions;
    object[QStringLiteral("isTagged")] = analysis.isTagged;
    object[QStringLiteral("visibleCharacterCount")] = analysis.visibleCharacterCount;
    object[QStringLiteral("invisibleCharacterCount")] = analysis.invisibleCharacterCount;
    object[QStringLiteral("imageCount")] = analysis.imageCount;
    object[QStringLiteral("unmappedCharacterCount")] = analysis.unmappedCharacterCount;
    object[QStringLiteral("ambiguityReasons")] = toJsonArray(analysis.ambiguityReasons);
    object[QStringLiteral("notes")] = toJsonArray(analysis.notes);
    object[QStringLiteral("ownLayerId")] = analysis.ownLayerId;

    QJsonArray annotationRectangles;
    for (const QRectF& rect : analysis.annotationRectangles)
    {
        annotationRectangles.append(rectToJson(rect));
    }
    object[QStringLiteral("annotationRectangles")] = annotationRectangles;

    QJsonArray redactionRectangles;
    for (const QRectF& rect : analysis.redactionRectangles)
    {
        redactionRectangles.append(rectToJson(rect));
    }
    object[QStringLiteral("redactionRectangles")] = redactionRectangles;

    QJsonArray textRectangles;
    for (const QRectF& rect : analysis.textRectangles)
    {
        textRectangles.append(rectToJson(rect));
    }
    object[QStringLiteral("textRectangles")] = textRectangles;
    return object;
}

PDFOCRPageAnalysis PDFOCRProjectSerializer::analysisFromJson(const QJsonObject& object)
{
    PDFOCRPageAnalysis analysis;
    analysis.contentClass = PDFOCREnumerations::toPageContentClass(object.value(QStringLiteral("contentClass")).toString());
    analysis.hasImages = object.value(QStringLiteral("hasImages")).toBool();
    analysis.hasVisibleText = object.value(QStringLiteral("hasVisibleText")).toBool();
    analysis.hasInvisibleText = object.value(QStringLiteral("hasInvisibleText")).toBool();
    analysis.hasOwnOCRLayer = object.value(QStringLiteral("hasOwnOCRLayer")).toBool();
    analysis.hasAnnotations = object.value(QStringLiteral("hasAnnotations")).toBool();
    analysis.hasFormFields = object.value(QStringLiteral("hasFormFields")).toBool();
    analysis.hasUnappliedRedactions = object.value(QStringLiteral("hasUnappliedRedactions")).toBool();
    analysis.isTagged = object.value(QStringLiteral("isTagged")).toBool();
    analysis.visibleCharacterCount = object.value(QStringLiteral("visibleCharacterCount")).toInt();
    analysis.invisibleCharacterCount = object.value(QStringLiteral("invisibleCharacterCount")).toInt();
    analysis.imageCount = object.value(QStringLiteral("imageCount")).toInt();
    analysis.unmappedCharacterCount = object.value(QStringLiteral("unmappedCharacterCount")).toInt();
    analysis.ambiguityReasons = toStringList(object.value(QStringLiteral("ambiguityReasons")));
    analysis.notes = toStringList(object.value(QStringLiteral("notes")));
    analysis.ownLayerId = object.value(QStringLiteral("ownLayerId")).toString();
    for (const QJsonValue& value : object.value(QStringLiteral("annotationRectangles")).toArray())
    {
        analysis.annotationRectangles.push_back(rectFromJson(value));
    }
    for (const QJsonValue& value : object.value(QStringLiteral("redactionRectangles")).toArray())
    {
        analysis.redactionRectangles.push_back(rectFromJson(value));
    }
    for (const QJsonValue& value : object.value(QStringLiteral("textRectangles")).toArray())
    {
        analysis.textRectangles.push_back(rectFromJson(value));
    }
    return analysis;
}

QJsonObject PDFOCRProjectSerializer::errorToJson(const PDFOCRError& error)
{
    QJsonObject object;
    object[QStringLiteral("code")] = PDFOCRError::getCodeIdentifier(error.code);
    object[QStringLiteral("message")] = error.message;
    object[QStringLiteral("step")] = error.step;
    object[QStringLiteral("detail")] = error.detail;
    return object;
}

PDFOCRError PDFOCRProjectSerializer::errorFromJson(const QJsonObject& object)
{
    PDFOCRError error;
    error.code = PDFOCRError::parseCodeIdentifier(object.value(QStringLiteral("code")).toString(QStringLiteral("none")));
    error.message = object.value(QStringLiteral("message")).toString();
    error.step = object.value(QStringLiteral("step")).toString();
    error.detail = object.value(QStringLiteral("detail")).toString();
    return error;
}

QJsonObject PDFOCRProjectSerializer::orientationToJson(const PDFOCROrientation& orientation)
{
    QJsonObject object;
    object[QStringLiteral("rotation")] = orientation.rotation;
    if (orientation.confidence)
    {
        object[QStringLiteral("confidence")] = *orientation.confidence;
    }
    object[QStringLiteral("deskewAngle")] = orientation.deskewAngle;
    if (orientation.deskewConfidence)
    {
        object[QStringLiteral("deskewConfidence")] = *orientation.deskewConfidence;
    }
    object[QStringLiteral("script")] = orientation.script;
    return object;
}

PDFOCROrientation PDFOCRProjectSerializer::orientationFromJson(const QJsonObject& object)
{
    PDFOCROrientation orientation;
    orientation.rotation = object.value(QStringLiteral("rotation")).toInt();
    if (object.contains(QStringLiteral("confidence")))
    {
        orientation.confidence = object.value(QStringLiteral("confidence")).toDouble();
    }
    orientation.deskewAngle = object.value(QStringLiteral("deskewAngle")).toDouble();
    if (object.contains(QStringLiteral("deskewConfidence")))
    {
        orientation.deskewConfidence = object.value(QStringLiteral("deskewConfidence")).toDouble();
    }
    orientation.script = object.value(QStringLiteral("script")).toString();
    return orientation;
}

QJsonObject PDFOCRProjectSerializer::pageResultToJson(const PDFOCRPageResult& result, PDFOCRSerializationFlags flags)
{
    QJsonObject object;
    object[QStringLiteral("pageIndex")] = qint64(result.pageIndex);
    object[QStringLiteral("pageLabel")] = result.pageLabel;
    object[QStringLiteral("pageFingerprint")] = QString::fromLatin1(result.pageFingerprint.toHex());
    object[QStringLiteral("state")] = PDFOCREnumerations::toString(result.state);
    object[QStringLiteral("error")] = errorToJson(result.error);
    object[QStringLiteral("skipReason")] = result.skipReason;
    object[QStringLiteral("generation")] = result.generation;
    object[QStringLiteral("nextId")] = result.nextId;
    object[QStringLiteral("recognitionTime")] = dateTimeToJson(result.recognitionTime);
    object[QStringLiteral("elapsedMilliseconds")] = qint64(result.elapsedMilliseconds);
    object[QStringLiteral("blankDetectionOverridden")] = result.blankDetectionOverridden;
    object[QStringLiteral("isModified")] = result.isModified;
    object[QStringLiteral("reviewOnly")] = result.reviewOnly;

    if (flags.testFlag(PDFOCRSerializationFlag::Analysis))
    {
        object[QStringLiteral("analysis")] = analysisToJson(result.analysis);
    }

    if (flags.testFlag(PDFOCRSerializationFlag::Geometry))
    {
        object[QStringLiteral("geometry")] = geometryToJson(result.geometry);
        object[QStringLiteral("provenance")] = provenanceToJson(result.provenance);
        if (result.orientation)
        {
            object[QStringLiteral("orientation")] = orientationToJson(*result.orientation);
        }
    }

    if (flags.testFlag(PDFOCRSerializationFlag::Regions))
    {
        QJsonArray regions;
        for (const PDFOCRRegion& region : result.regions)
        {
            regions.append(regionToJson(region));
        }
        object[QStringLiteral("regions")] = regions;
    }

    QJsonArray blocks;
    for (const PDFOCRBlock& block : result.blocks)
    {
        blocks.append(blockToJson(block, flags));
    }
    object[QStringLiteral("blocks")] = blocks;

    // Raw recognition (DATA-02) is stored only together with the review data
    if (flags.testFlag(PDFOCRSerializationFlag::ReviewData) && !result.originalBlocks.empty())
    {
        QJsonArray originalBlocks;
        for (const PDFOCRBlock& block : result.originalBlocks)
        {
            originalBlocks.append(blockToJson(block, flags));
        }
        object[QStringLiteral("originalBlocks")] = originalBlocks;
    }
    return object;
}

PDFOCRPageResult PDFOCRProjectSerializer::pageResultFromJson(const QJsonObject& object)
{
    PDFOCRPageResult result;
    pageResultFromJson(object, result, nullptr);
    return result;
}

bool PDFOCRProjectSerializer::pageResultFromJson(const QJsonObject& object, PDFOCRPageResult& result, QString* errorMessage)
{
    result = PDFOCRPageResult();
    result.pageIndex = PDFInteger(object.value(QStringLiteral("pageIndex")).toDouble(-1));
    result.pageLabel = object.value(QStringLiteral("pageLabel")).toString();
    result.pageFingerprint = QByteArray::fromHex(object.value(QStringLiteral("pageFingerprint")).toString().toLatin1());
    result.state = PDFOCREnumerations::toPageState(object.value(QStringLiteral("state")).toString());
    result.error = errorFromJson(object.value(QStringLiteral("error")).toObject());
    result.skipReason = object.value(QStringLiteral("skipReason")).toString();
    result.generation = object.value(QStringLiteral("generation")).toInt();
    result.nextId = object.value(QStringLiteral("nextId")).toInt(1);
    result.recognitionTime = dateTimeFromJson(object.value(QStringLiteral("recognitionTime")));
    result.elapsedMilliseconds = qint64(object.value(QStringLiteral("elapsedMilliseconds")).toDouble());
    result.blankDetectionOverridden = object.value(QStringLiteral("blankDetectionOverridden")).toBool();
    result.isModified = object.value(QStringLiteral("isModified")).toBool();
    result.reviewOnly = object.value(QStringLiteral("reviewOnly")).toBool();

    if (object.contains(QStringLiteral("analysis")))
    {
        result.analysis = analysisFromJson(object.value(QStringLiteral("analysis")).toObject());
    }
    if (object.contains(QStringLiteral("geometry")))
    {
        result.geometry = geometryFromJson(object.value(QStringLiteral("geometry")).toObject());
    }
    if (object.contains(QStringLiteral("provenance")))
    {
        result.provenance = provenanceFromJson(object.value(QStringLiteral("provenance")).toObject());
    }
    if (object.contains(QStringLiteral("orientation")))
    {
        result.orientation = orientationFromJson(object.value(QStringLiteral("orientation")).toObject());
    }

    // Limits of the data model are enforced while parsing (DATA-03, OPS-05), so a
    // hostile or damaged file cannot build an unbounded object tree.
    const QJsonArray regions = object.value(QStringLiteral("regions")).toArray();
    if (regions.size() > PDFOCRValidator::MaximumRegionsPerPage)
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Page %1 has %2 regions, at most %3 regions per page are allowed.").arg(result.pageIndex + 1).arg(regions.size()).arg(PDFOCRValidator::MaximumRegionsPerPage);
        }
        return false;
    }
    for (const QJsonValue& value : regions)
    {
        result.regions.push_back(regionFromJson(value.toObject()));
    }

    // Words are counted over the blocks before they are converted
    auto checkBlocks = [&](const QJsonArray& blocks, const char* what) -> bool
    {
        qint64 wordCount = 0;
        for (const QJsonValue& blockValue : blocks)
        {
            const QJsonArray lines = blockValue.toObject().value(QStringLiteral("lines")).toArray();
            for (const QJsonValue& lineValue : lines)
            {
                const QJsonArray words = lineValue.toObject().value(QStringLiteral("words")).toArray();
                wordCount += words.size();
                if (wordCount > PDFOCRValidator::MaximumWordsPerPage)
                {
                    if (errorMessage)
                    {
                        *errorMessage = PDFTranslationContext::tr("Page %1 has too many words (%2), at most %3 words per page are allowed.").arg(result.pageIndex + 1).arg(QLatin1String(what)).arg(PDFOCRValidator::MaximumWordsPerPage);
                    }
                    return false;
                }

                for (const QJsonValue& wordValue : words)
                {
                    const QJsonValue text = wordValue.toObject().value(QStringLiteral("text"));
                    if (text.toString().length() > PDFOCRValidator::MaximumTextLength)
                    {
                        if (errorMessage)
                        {
                            *errorMessage = PDFTranslationContext::tr("Page %1 (%2) contains a word text of %3 characters, at most %4 characters are allowed.").arg(result.pageIndex + 1).arg(QLatin1String(what)).arg(text.toString().length()).arg(PDFOCRValidator::MaximumTextLength);
                        }
                        return false;
                    }
                }
            }
        }
        return true;
    };

    const QJsonArray originalBlocks = object.value(QStringLiteral("originalBlocks")).toArray();
    const QJsonArray blocks = object.value(QStringLiteral("blocks")).toArray();
    if (!checkBlocks(originalBlocks, "original recognition") || !checkBlocks(blocks, "blocks"))
    {
        return false;
    }

    for (const QJsonValue& value : originalBlocks)
    {
        result.originalBlocks.push_back(blockFromJson(value.toObject()));
    }
    for (const QJsonValue& value : blocks)
    {
        result.blocks.push_back(blockFromJson(value.toObject()));
    }

    result.assignIdentifiers();
    return true;
}

QJsonObject PDFOCRProjectSerializer::projectToJson(const PDFOCRProject& project)
{
    QJsonObject object;
    object[QStringLiteral("format")] = QLatin1String(PDFOCRProject::FORMAT_IDENTIFIER);
    object[QStringLiteral("version")] = PDFOCRProject::FORMAT_VERSION;
    object[QStringLiteral("application")] = project.application.isEmpty() ? QString::fromLatin1(PDF_LIBRARY_NAME) : project.application;
    object[QStringLiteral("created")] = dateTimeToJson(project.created);
    object[QStringLiteral("modified")] = dateTimeToJson(project.modified);
    object[QStringLiteral("containsOriginalTexts")] = project.containsOriginalTexts;
    object[QStringLiteral("containsPreviews")] = project.containsPreviews;
    object[QStringLiteral("document")] = project.document.toJson();
    object[QStringLiteral("configuration")] = project.configuration.toJson();
    object[QStringLiteral("selectedPages")] = toJsonArray(project.selectedPages);

    QJsonObject overrides;
    for (const auto& item : project.pageOverrides)
    {
        overrides[QString::number(item.first)] = item.second.toJson();
    }
    object[QStringLiteral("pageOverrides")] = overrides;

    QJsonArray pages;
    for (const auto& item : project.pages)
    {
        pages.append(pageResultToJson(item.second, PDFOCRSerializationFlag::All));
    }
    object[QStringLiteral("pages")] = pages;
    return object;
}

bool PDFOCRProjectSerializer::projectFromJson(const QJsonObject& object, PDFOCRProject& project, QString* errorMessage)
{
    if (object.value(QStringLiteral("format")).toString() != QLatin1String(PDFOCRProject::FORMAT_IDENTIFIER))
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("File is not a PDF4QT OCR project.");
        }
        return false;
    }

    const int version = object.value(QStringLiteral("version")).toInt();
    if (version > PDFOCRProject::FORMAT_VERSION || version < 1)
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Unsupported project version %1.").arg(version);
        }
        return false;
    }

    project = PDFOCRProject();
    project.application = object.value(QStringLiteral("application")).toString();
    project.created = dateTimeFromJson(object.value(QStringLiteral("created")));
    project.modified = dateTimeFromJson(object.value(QStringLiteral("modified")));
    project.containsOriginalTexts = object.value(QStringLiteral("containsOriginalTexts")).toBool(true);
    project.containsPreviews = object.value(QStringLiteral("containsPreviews")).toBool(false);
    project.document = PDFOCRDocumentIdentity::fromJson(object.value(QStringLiteral("document")).toObject());
    project.configuration = PDFOCRConfiguration::fromJson(object.value(QStringLiteral("configuration")).toObject());
    project.selectedPages = toIntegerVector(object.value(QStringLiteral("selectedPages")));

    const QJsonObject overrides = object.value(QStringLiteral("pageOverrides")).toObject();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
    {
        bool ok = false;
        const PDFInteger pageIndex = it.key().toLongLong(&ok);
        if (ok)
        {
            project.pageOverrides[pageIndex] = PDFOCRPageOverride::fromJson(it.value().toObject());
        }
    }

    const QJsonArray pages = object.value(QStringLiteral("pages")).toArray();
    if (pages.size() > PDFOCRValidator::MaximumPages)
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Project has %1 pages, at most %2 pages are allowed.").arg(pages.size()).arg(PDFOCRValidator::MaximumPages);
        }
        project = PDFOCRProject();
        return false;
    }

    for (const QJsonValue& value : pages)
    {
        PDFOCRPageResult result;
        if (!pageResultFromJson(value.toObject(), result, errorMessage))
        {
            project = PDFOCRProject();
            return false;
        }
        if (result.pageIndex >= 0)
        {
            project.pages[result.pageIndex] = std::move(result);
        }
    }

    return true;
}

bool PDFOCRProjectSerializer::save(const PDFOCRProject& project, const QString& fileName, QString* errorMessage)
{
    QSaveFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Cannot open file '%1' for writing: %2").arg(fileName, file.errorString());
        }
        return false;
    }

    file.write(toBytes(project));

    if (!file.commit())
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Cannot write file '%1': %2").arg(fileName, file.errorString());
        }
        return false;
    }

    return true;
}

bool PDFOCRProjectSerializer::load(const QString& fileName, PDFOCRProject& project, QString* errorMessage)
{
    QFile file(fileName);
    if (!file.open(QFile::ReadOnly))
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Cannot open file '%1': %2").arg(fileName, file.errorString());
        }
        return false;
    }

    // Size of the input is limited before anything is read (OPS-05)
    if (file.size() > PDFOCRValidator::MaximumProjectFileSize)
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Project file '%1' is too large (%2 MB), at most %3 MB are allowed.").arg(fileName).arg(file.size() / (1024 * 1024)).arg(PDFOCRValidator::MaximumProjectFileSize / (1024 * 1024));
        }
        return false;
    }

    return fromBytes(file.readAll(), project, errorMessage);
}

QByteArray PDFOCRProjectSerializer::toBytes(const PDFOCRProject& project)
{
    return QJsonDocument(projectToJson(project)).toJson(QJsonDocument::Compact);
}

bool PDFOCRProjectSerializer::fromBytes(const QByteArray& data, PDFOCRProject& project, QString* errorMessage)
{
    if (qint64(data.size()) > PDFOCRValidator::MaximumProjectFileSize)
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Project data are too large (%1 MB), at most %2 MB are allowed.").arg(qint64(data.size()) / (1024 * 1024)).arg(PDFOCRValidator::MaximumProjectFileSize / (1024 * 1024));
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (document.isNull() || !document.isObject())
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Invalid project file: %1").arg(parseError.errorString());
        }
        return false;
    }

    return projectFromJson(document.object(), project, errorMessage);
}

PDFOCRProjectSerializer::MatchResult PDFOCRProjectSerializer::match(const PDFOCRProject& project,
                                                                    const PDFOCRDocumentIdentity& identity,
                                                                    const std::function<QByteArray(PDFInteger)>& pageFingerprintGetter)
{
    MatchResult result;
    result.documentMatches = !project.document.fingerprint.isEmpty() && project.document.fingerprint == identity.fingerprint && project.document.pageCount == identity.pageCount;

    for (const auto& item : project.pages)
    {
        const PDFInteger pageIndex = item.first;
        if (pageIndex < 0 || pageIndex >= identity.pageCount)
        {
            result.missingPages.push_back(pageIndex);
            continue;
        }

        const QByteArray fingerprint = pageFingerprintGetter ? pageFingerprintGetter(pageIndex) : QByteArray();
        if (!item.second.pageFingerprint.isEmpty() && item.second.pageFingerprint == fingerprint)
        {
            result.matchingPages.push_back(pageIndex);
        }
        else
        {
            result.changedPages.push_back(pageIndex);
        }
    }

    return result;
}

// -------------------------------------------------------------------------
// PDFOCRTextExporter
// -------------------------------------------------------------------------

QString PDFOCRTextExporter::getPageStateDescription(const PDFOCRPageResult& result)
{
    switch (result.state)
    {
        case PDFOCRPageState::Pending:
            return PDFTranslationContext::tr("not recognized");

        case PDFOCRPageState::Preparing:
        case PDFOCRPageState::Recognizing:
            return PDFTranslationContext::tr("recognition in progress");

        case PDFOCRPageState::Done:
            return PDFTranslationContext::tr("recognized");

        case PDFOCRPageState::NoText:
            return result.skipReason.isEmpty() ? PDFTranslationContext::tr("no text was recognized") : result.skipReason;

        case PDFOCRPageState::Skipped:
            return result.skipReason.isEmpty() ? PDFTranslationContext::tr("skipped") : PDFTranslationContext::tr("skipped, %1").arg(result.skipReason);

        case PDFOCRPageState::Error:
            return result.error.message.isEmpty() ? PDFTranslationContext::tr("recognition failed") : PDFTranslationContext::tr("recognition failed, %1").arg(result.error.message);

        case PDFOCRPageState::Cancelled:
            return PDFTranslationContext::tr("recognition was cancelled");

        case PDFOCRPageState::Stale:
            return PDFTranslationContext::tr("result is stale (settings changed)");
    }

    return QString();
}

QString PDFOCRTextExporter::getPageText(const PDFOCRPageResult& result, const Options& options)
{
    QStringList blockTexts;

    for (const PDFOCRBlock& block : result.blocks)
    {
        QStringList lineTexts;
        for (const PDFOCRLine& line : block.lines)
        {
            QStringList words;
            for (const PDFOCRWord& word : line.words)
            {
                if (word.reviewState == PDFOCRReviewState::Discarded)
                {
                    continue;
                }

                if (options.onlyReviewed && word.reviewState == PDFOCRReviewState::Unreviewed)
                {
                    continue;
                }

                if (word.text.trimmed().isEmpty())
                {
                    continue;
                }

                words << word.text;
            }

            if (!words.isEmpty())
            {
                lineTexts << words.join(QChar(' '));
            }
        }

        if (lineTexts.isEmpty())
        {
            continue;
        }

        QString blockText = lineTexts.join(options.preserveLines ? QStringLiteral("\n") : QStringLiteral(" "));
        if (options.joinHyphenatedWords && options.preserveLines)
        {
            blockText = joinHyphenatedLines(blockText);
        }
        blockTexts << blockText;
    }

    QString text = blockTexts.join(QStringLiteral("\n\n"));
    if (options.normalizeNFC)
    {
        text = text.normalized(QString::NormalizationForm_C);
    }
    return text;
}

QString PDFOCRTextExporter::exportText(const std::vector<const PDFOCRPageResult*>& pages, const Options& options, Report* report)
{
    QStringList pageTexts;

    for (const PDFOCRPageResult* page : pages)
    {
        if (!page)
        {
            continue;
        }

        QString description = PDFTranslationContext::tr("Page %1").arg(page->pageIndex + 1);
        if (!page->pageLabel.isEmpty() && page->pageLabel != QString::number(page->pageIndex + 1))
        {
            description += QStringLiteral(" (%1)").arg(page->pageLabel);
        }

        if (!page->hasResult())
        {
            if (report)
            {
                report->skippedPages.push_back(page->pageIndex);
                report->skippedDescriptions << QStringLiteral("%1: %2").arg(description, getPageStateDescription(*page));
            }
            continue;
        }

        if (page->state == PDFOCRPageState::NoText && report)
        {
            report->noTextDescriptions << QStringLiteral("%1: %2").arg(description, getPageStateDescription(*page));
        }

        QString pageText = getPageText(*page, options);

        switch (options.pageSeparator)
        {
            case PageSeparator::None:
                break;

            case PageSeparator::FormFeed:
                if (!pageTexts.isEmpty())
                {
                    pageText.prepend(QChar(0x0C));
                }
                break;

            case PageSeparator::Label:
                pageText.prepend(QStringLiteral("==== %1 ====\n").arg(description));
                break;
        }

        pageTexts << pageText;

        if (report)
        {
            report->exportedPages.push_back(page->pageIndex);
            report->pageDescriptions << description;
            report->wordCount += page->getWordCount();

            QStringList regionNames;
            for (const PDFOCRBlock& block : page->blocks)
            {
                if (block.regionId != -1)
                {
                    if (const PDFOCRRegion* region = page->findRegion(block.regionId))
                    {
                        const QString name = region->name.isEmpty() ? PDFTranslationContext::tr("Region %1").arg(region->id) : region->name;
                        if (!regionNames.contains(name))
                        {
                            regionNames << name;
                        }
                    }
                }
            }

            if (!regionNames.isEmpty())
            {
                report->regionOrders << QStringLiteral("%1: %2").arg(description, regionNames.join(QStringLiteral(", ")));
            }
        }
    }

    return pageTexts.join(QStringLiteral("\n\n"));
}

bool PDFOCRTextExporter::writeTextFile(const QString& fileName, const QString& text, QString* errorMessage)
{
    QSaveFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Cannot open file '%1' for writing: %2").arg(fileName, file.errorString());
        }
        return false;
    }

    file.write(text.toUtf8());

    if (!file.commit())
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Cannot write file '%1': %2").arg(fileName, file.errorString());
        }
        return false;
    }

    return true;
}

QString PDFOCRTextExporter::joinHyphenatedLines(const QString& text)
{
    QStringList lines = text.split(QChar('\n'));

    for (int i = 0; i + 1 < lines.size(); ++i)
    {
        QString& line = lines[i];
        if (!line.endsWith(QChar('-')))
        {
            continue;
        }

        const QString next = lines[i + 1].trimmed();

        // Only lowercase continuation is joined (real hyphens before uppercase words are kept)
        if (next.isEmpty() || !next.front().isLower())
        {
            continue;
        }

        line.chop(1);
        const int spaceIndex = next.indexOf(QChar(' '));
        if (spaceIndex == -1)
        {
            line += next;
            lines[i + 1].clear();
        }
        else
        {
            line += next.left(spaceIndex);
            lines[i + 1] = next.mid(spaceIndex + 1);
        }
    }

    lines.removeAll(QString());
    return lines.join(QChar('\n'));
}

}   // namespace pdf
