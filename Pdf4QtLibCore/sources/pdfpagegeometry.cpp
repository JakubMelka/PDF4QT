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

#include "pdfpagegeometry.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfoperationcontrol.h"
#include "pdfparser.h"
#include "pdfrenderer.h"

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "pdfdbgheap.h"

namespace pdf
{

/// What happens with an object of the document, which belongs to a page with moved content
class PDFPageGeometryRule
{
public:
    enum class Kind
    {
        Annotation,         ///< An annotation of the page (12.5.2)
        Viewport,           ///< A viewport of /VP of the page (12.9.1)
        Measure,            ///< A measure dictionary of a viewport or an annotation (12.9.2)
        NumberFormat,       ///< The first number format of an axis of a measure: /C is divided
        Bead,               ///< A bead of an article on the page (12.4.3)
        LayoutAttribute     ///< A layout attribute of a structure element of the page (14.8.5.4.3)
    };

    Kind kind = Kind::Annotation;
    QTransform transform;           ///< The move of the content
    bool scaleAnnotation = true;    ///< Annotation: scaled with the content (else only moved)
    PDFReal factor = 1.0;           ///< NumberFormat: the divisor of /C
};

/// Balance of the operators of the whole content of a page (7.8.2, 8.4.2)
class PDFPageGeometryContentBalance
{
public:
    int underflow = 0;      ///< The most "Q" over "q" at any place of the content (hostile content)
    int depth = 0;          ///< "q" minus "Q" at the end
    int textDepth = 0;      ///< Unclosed "BT"
    int markedDepth = 0;    ///< Unclosed "BMC" / "BDC"
};

/// Helpers of PDFPageGeometry
class PDFPageGeometryHelper
{
public:
    PDFPageGeometryHelper() = delete;

    /// Deepest walk into direct objects of an object (a hostile file is not a stack overflow)
    static constexpr int MAXIMUM_DEPTH = 64;

    /// Bit NoZoom of the annotation flags (Table 167)
    static constexpr PDFInteger ANNOTATION_NO_ZOOM = 8;

    static PDFObject createNumber(PDFReal value)
    {
        return PDFObject::createReal(value);
    }

    static PDFObject createRectangle(const QRectF& rect)
    {
        const QRectF normalized = rect.normalized();
        PDFArrayBuilder array;
        array.appendItem(createNumber(normalized.left()));
        array.appendItem(createNumber(normalized.top()));
        array.appendItem(createNumber(normalized.right()));
        array.appendItem(createNumber(normalized.bottom()));
        return PDFObject::createArray(std::move(array));
    }

    static PDFObject createMatrix(const QTransform& matrix)
    {
        PDFArrayBuilder array;
        for (const PDFReal value : { matrix.m11(), matrix.m12(), matrix.m21(), matrix.m22(), matrix.dx(), matrix.dy() })
        {
            array.appendItem(createNumber(value));
        }
        return PDFObject::createArray(std::move(array));
    }

    /// Number of a direct object, std::nullopt for anything else
    static std::optional<PDFReal> readNumber(const PDFObject& object)
    {
        if (object.isReal())
        {
            return object.getReal();
        }
        if (object.isInt())
        {
            return PDFReal(object.getInteger());
        }
        return std::nullopt;
    }

    /// Dictionary of a direct object (a dictionary or the dictionary of a stream), nullptr = none
    static const PDFDictionary* getDictionary(const PDFObject& object)
    {
        if (object.isDictionary())
        {
            return object.getDictionary();
        }
        if (object.isStream())
        {
            return object.getStream()->getDictionary();
        }
        return nullptr;
    }

    /// A copy of a dictionary or of a stream with other entries (a null value removes the key)
    static PDFObject replaceEntries(const PDFObject& object, const std::vector<std::pair<QByteArray, PDFObject>>& entries)
    {
        const PDFDictionary* dictionary = getDictionary(object);
        if (!dictionary || entries.empty())
        {
            return object;
        }

        PDFDictionaryBuilder builder(*dictionary);
        for (const auto& [key, value] : entries)
        {
            if (value.isNull())
            {
                builder.removeEntry(key.constData());
            }
            else
            {
                builder.setEntry(PDFInplaceOrMemoryString(key), value);
            }
        }

        if (object.isStream())
        {
            return PDFObject::createStream(PDFStream(std::move(builder), QByteArray(*object.getStream()->getContent())));
        }
        return PDFObject::createDictionary(std::move(builder));
    }

    /// The matrix has no rotation and no skew (the move of a page keeps its /Rotate)
    static bool isDiagonal(const QTransform& transform)
    {
        return qFuzzyIsNull(transform.m12()) && qFuzzyIsNull(transform.m21());
    }

    /// Scale of the lengths of the matrix along x and y
    static PDFReal getScaleX(const QTransform& transform)
    {
        return std::hypot(transform.m11(), transform.m12());
    }

    static PDFReal getScaleY(const QTransform& transform)
    {
        return std::hypot(transform.m21(), transform.m22());
    }

    /// Scale of a length in any direction (the geometric mean)
    static PDFReal getScale(const QTransform& transform)
    {
        return std::sqrt(getScaleX(transform) * getScaleY(transform));
    }

    /// A flat array of coordinate pairs transformed, std::nullopt when it is no such array
    static std::optional<PDFObject> transformCoordinates(const PDFObjectStorage* storage, const PDFObject& object, const QTransform& transform)
    {
        const PDFObject& dereferenced = storage->getObject(object);
        if (!dereferenced.isArray() || dereferenced.getArray()->getCount() % 2 != 0)
        {
            return std::nullopt;
        }

        const PDFArray* array = dereferenced.getArray();
        PDFArrayBuilder result;
        result.reserve(array->getCount());
        for (size_t i = 0; i < array->getCount(); i += 2)
        {
            const std::optional<PDFReal> x = readNumber(storage->getObject(array->getItem(i)));
            const std::optional<PDFReal> y = readNumber(storage->getObject(array->getItem(i + 1)));
            if (!x || !y)
            {
                return std::nullopt;
            }
            const QPointF point = transform.map(QPointF(*x, *y));
            result.appendItem(createNumber(point.x()));
            result.appendItem(createNumber(point.y()));
        }
        return PDFObject::createArray(std::move(result));
    }

    /// An array of arrays of coordinate pairs (/InkList, Table 182; /Path, Table 185)
    static std::optional<PDFObject> transformCoordinateLists(const PDFObjectStorage* storage, const PDFObject& object, const QTransform& transform)
    {
        const PDFObject& dereferenced = storage->getObject(object);
        if (!dereferenced.isArray())
        {
            return std::nullopt;
        }

        PDFArrayBuilder result;
        for (const PDFObject& item : *dereferenced.getArray())
        {
            std::optional<PDFObject> transformed = transformCoordinates(storage, item, transform);
            if (!transformed)
            {
                return std::nullopt;
            }
            result.appendItem(std::move(*transformed));
        }
        return PDFObject::createArray(std::move(result));
    }

    /// Lengths multiplied (/RD of Table 180, /LL, /LLE, /LLO of Table 177)
    static std::optional<PDFObject> scaleNumbers(const PDFObjectStorage* storage, const PDFObject& object, const std::vector<PDFReal>& factors)
    {
        const PDFObject& dereferenced = storage->getObject(object);
        if (const std::optional<PDFReal> number = readNumber(dereferenced); number && factors.size() == 1)
        {
            return createNumber(*number * factors.front());
        }
        if (!dereferenced.isArray() || dereferenced.getArray()->getCount() != factors.size())
        {
            return std::nullopt;
        }

        PDFArrayBuilder result;
        for (size_t i = 0; i < factors.size(); ++i)
        {
            const std::optional<PDFReal> number = readNumber(storage->getObject(dereferenced.getArray()->getItem(i)));
            if (!number)
            {
                return std::nullopt;
            }
            result.appendItem(createNumber(*number * factors[i]));
        }
        return PDFObject::createArray(std::move(result));
    }

    /// The balance of the operators of the content of a page (inline images skipped)
    static PDFPageGeometryContentBalance getContentBalance(const PDFDocument* document, const PDFPage* page)
    {
        PDFPageGeometryContentBalance balance;
        const PDFObjectStorage* storage = &document->getStorage();

        // 7.8.2: the streams are one content, the boundaries are only between tokens
        QByteArray content;
        const PDFObject& contents = storage->getObject(page->getContents());
        std::vector<PDFObject> streams;
        if (contents.isStream())
        {
            streams.push_back(contents);
        }
        else if (contents.isArray())
        {
            for (const PDFObject& item : *contents.getArray())
            {
                const PDFObject& stream = storage->getObject(item);
                if (stream.isStream())
                {
                    streams.push_back(stream);
                }
            }
        }
        for (const PDFObject& stream : streams)
        {
            content.append(document->getDecodedStream(stream.getStream()));
            content.append('\n');
        }

        try
        {
            PDFLexicalAnalyzer analyzer(content.constBegin(), content.constEnd());
            while (true)
            {
                const PDFLexicalAnalyzer::Token token = analyzer.fetch();
                if (token.type == PDFLexicalAnalyzer::TokenType::EndOfFile)
                {
                    break;
                }
                if (token.type != PDFLexicalAnalyzer::TokenType::Command)
                {
                    continue;
                }

                const QByteArray command = token.data.toByteArray();
                if (command == "q")
                {
                    ++balance.depth;
                }
                else if (command == "Q")
                {
                    --balance.depth;
                    balance.underflow = qMax(balance.underflow, -balance.depth);
                }
                else if (command == "BT")
                {
                    ++balance.textDepth;
                }
                else if (command == "ET")
                {
                    balance.textDepth = qMax(0, balance.textDepth - 1);
                }
                else if (command == "BMC" || command == "BDC")
                {
                    ++balance.markedDepth;
                }
                else if (command == "EMC")
                {
                    balance.markedDepth = qMax(0, balance.markedDepth - 1);
                }
                else if (command == "BI")
                {
                    // The data of an inline image are no tokens
                    const PDFInteger end = analyzer.findSubstring("EI", analyzer.pos());
                    if (end == -1)
                    {
                        break;
                    }
                    analyzer.seek(end + 2);
                }
            }
        }
        catch (const PDFException&)
        {
            // The renderer stops at the same error: what was read decides
        }
        return balance;
    }

    /// The stream put before the content: the matrix, the clip and a "q" for every excess "Q"
    static QByteArray createContentPrefix(const QTransform& transform, const QRectF& clip, const PDFPageGeometryContentBalance& balance)
    {
        QByteArray prefix = "q\n";
        for (const PDFReal value : { transform.m11(), transform.m12(), transform.m21(), transform.m22(), transform.dx(), transform.dy() })
        {
            prefix.append(PDFDocumentBuilder::formatPDFReal(value));
            prefix.append(' ');
        }
        prefix.append("cm\n");
        if (clip.isValid())
        {
            for (const PDFReal value : { clip.left(), clip.top(), clip.width(), clip.height() })
            {
                prefix.append(PDFDocumentBuilder::formatPDFReal(value));
                prefix.append(' ');
            }
            prefix.append("re W n\n");
        }

        // R7: the "q" for the excess "Q" of the content are after "cm", so they cannot undo it
        for (int i = 0; i < balance.underflow; ++i)
        {
            prefix.append("q\n");
        }
        return prefix;
    }

    /// The stream put after the content: everything closed, the matrix too
    static QByteArray createContentSuffix(const PDFPageGeometryContentBalance& balance)
    {
        QByteArray suffix = "\n";
        for (int i = 0; i < balance.textDepth; ++i)
        {
            suffix.append("ET\n");
        }
        for (int i = 0; i < balance.markedDepth; ++i)
        {
            suffix.append("EMC\n");
        }
        const int closeCount = 1 + balance.underflow + balance.depth;
        for (int i = 0; i < qMax(1, closeCount); ++i)
        {
            suffix.append("Q\n");
        }
        return suffix;
    }

    static PDFObjectReference addStream(PDFDocumentBuilder* builder, QByteArray content)
    {
        PDFDictionaryBuilder dictionary;
        dictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
        return builder->addObject(PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content))));
    }

    /// The streams of the content of the page (a direct stream becomes an object)
    static std::vector<PDFObjectReference> getContentStreams(PDFDocumentBuilder* builder, const PDFObjectStorage* storage, const PDFObject& contents)
    {
        std::vector<PDFObjectReference> references;
        const PDFObject& dereferenced = storage->getObject(contents);
        if (dereferenced.isStream())
        {
            references.push_back(contents.isReference() ? contents.getReference() : builder->addObject(dereferenced));
        }
        else if (dereferenced.isArray())
        {
            for (const PDFObject& item : *dereferenced.getArray())
            {
                const PDFObject& stream = storage->getObject(item);
                if (stream.isStream())
                {
                    references.push_back(item.isReference() ? item.getReference() : builder->addObject(stream));
                }
            }
        }
        return references;
    }
};

/// One pass of PDFPageGeometry::applyChanges
class PDFPageGeometryEngine
{
public:
    PDFPageGeometryEngine(const PDFDocument* document, const std::vector<PDFPageGeometryChange>& changes, const PDFOperationControl* control);

    PDFDocument run(PDFPageGeometryStatistics* statistics);

private:
    using Helper = PDFPageGeometryHelper;
    using Rule = PDFPageGeometryRule;

    void collectRules();
    void addMeasureRules(const PDFObject& measure, const QTransform& transform);
    void addNumberFormatRule(const PDFObject& formats, PDFReal factor);

    PDFObject applyRule(const PDFObject& object, const Rule& rule);
    PDFObject transformAnnotation(const PDFObject& object, const QTransform& transform, bool scale);
    PDFObject transformViewport(const PDFObject& object, const QTransform& transform);
    PDFObject transformMeasure(const PDFObject& object, const QTransform& transform);
    PDFObject transformNumberFormat(const PDFObject& object, PDFReal factor);
    PDFObject transformNumberFormats(const PDFObject& formats, PDFReal factor);
    PDFObject transformStructureElement(const PDFObject& object, const QTransform& transform);
    PDFObject transformLayoutAttribute(const PDFObject& object, const QTransform& transform, bool* isChanged);
    PDFObject transformPage(const PDFObject& object, const PDFPage* page, const PDFPageGeometryChange& change);
    PDFObject createPatternResources(const PDFPage* page, const QTransform& transform);

    /// Destinations to the pages with moved content (direct objects, recursively)
    PDFObject transformDestinations(const PDFObject& object, int depth, bool* isChanged);
    std::optional<QTransform> getDestinationTransform(const PDFArray* array) const;
    PDFObject transformDestination(const PDFArray* array, const QTransform& transform);

    const PDFDocument* m_document;
    const PDFObjectStorage* m_storage;
    const PDFOperationControl* m_control;
    PDFDocumentBuilder m_builder;
    PDFPageGeometryStatistics m_statistics;

    std::map<PDFObjectReference, const PDFPageGeometryChange*> m_changes;     ///< Changes by the page objects (the first change of a page)
    std::map<PDFObjectReference, const PDFPage*> m_pages;                      ///< Pages of the changes
    std::map<PDFObjectReference, QTransform> m_movedPages;                     ///< Pages with moved content
    std::map<PDFObjectReference, Rule> m_rules;                                ///< Rules of the objects (the first rule of an object)
};

PDFPageGeometryEngine::PDFPageGeometryEngine(const PDFDocument* document, const std::vector<PDFPageGeometryChange>& changes, const PDFOperationControl* control) :
    m_document(document),
    m_storage(&document->getStorage()),
    m_control(control),
    m_builder(document)
{
    const PDFCatalog* catalog = document->getCatalog();
    for (const PDFPageGeometryChange& change : changes)
    {
        if (change.pageIndex < 0 || size_t(change.pageIndex) >= catalog->getPageCount())
        {
            throw PDFException(PDFPageGeometry::tr("The document has no page %1.").arg(change.pageIndex + 1));
        }

        const PDFPage* page = catalog->getPage(size_t(change.pageIndex));
        const PDFObjectReference reference = page->getPageReference();
        if (!reference.isValid() || !m_changes.emplace(reference, &change).second)
        {
            // A page object, which is in the page tree more times, changes once
            continue;
        }
        m_pages.emplace(reference, page);
        if (change.hasContentTransform())
        {
            m_movedPages.emplace(reference, *change.contentTransform);
        }
    }
}

void PDFPageGeometryEngine::addNumberFormatRule(const PDFObject& formats, PDFReal factor)
{
    // Table 268: an array of number formats; only the first one converts from the default units
    const PDFObject& array = m_storage->getObject(formats);
    if (!array.isArray() || array.getArray()->getCount() == 0)
    {
        return;
    }
    const PDFObject& first = array.getArray()->getItem(0);
    if (first.isReference())
    {
        Rule rule;
        rule.kind = Rule::Kind::NumberFormat;
        rule.factor = factor;
        m_rules.emplace(first.getReference(), rule);
    }
}

void PDFPageGeometryEngine::addMeasureRules(const PDFObject& measure, const QTransform& transform)
{
    if (measure.isReference())
    {
        Rule rule;
        rule.kind = Rule::Kind::Measure;
        rule.transform = transform;
        m_rules.emplace(measure.getReference(), rule);
    }

    // The number formats of an axis referenced from the measure
    const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(measure);
    if (!dictionary)
    {
        return;
    }
    const PDFReal scaleX = Helper::getScaleX(transform);
    const PDFReal scaleY = Helper::getScaleY(transform);
    const PDFReal scale = Helper::getScale(transform);
    const bool hasY = dictionary->hasKey("Y");
    addNumberFormatRule(dictionary->get("X"), hasY ? scaleX : scale);
    addNumberFormatRule(dictionary->get("Y"), scaleY);
    addNumberFormatRule(dictionary->get("D"), scale);
    addNumberFormatRule(dictionary->get("A"), scaleX * scaleY);
}

void PDFPageGeometryEngine::collectRules()
{
    for (const auto& [reference, transform] : m_movedPages)
    {
        const PDFPage* page = m_pages.at(reference);
        const PDFPageGeometryChange* change = m_changes.at(reference);

        // Annotations (an annotation in the /Annots of two pages moves with the first one)
        for (const PDFObjectReference annotation : page->getAnnotations())
        {
            Rule rule;
            rule.kind = Rule::Kind::Annotation;
            rule.transform = transform;
            rule.scaleAnnotation = change->scaleAnnotations;
            if (m_rules.emplace(annotation, rule).second)
            {
                if (const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(m_storage->getObjectByReference(annotation)))
                {
                    if (change->scaleAnnotations)
                    {
                        addMeasureRules(dictionary->get("Measure"), transform);
                    }
                }
            }
        }

        // Beads of the articles
        for (const PDFObjectReference bead : page->getBeads())
        {
            Rule rule;
            rule.kind = Rule::Kind::Bead;
            rule.transform = transform;
            m_rules.emplace(bead, rule);
        }

        // Viewports (by value: getObject returns its argument, a temporary, when it is no reference)
        const PDFObject viewports = m_storage->getObject(page->getViewports(m_storage));
        if (viewports.isArray())
        {
            for (const PDFObject& viewport : *viewports.getArray())
            {
                if (viewport.isReference())
                {
                    Rule rule;
                    rule.kind = Rule::Kind::Viewport;
                    rule.transform = transform;
                    m_rules.emplace(viewport.getReference(), rule);
                }
                if (const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(viewport))
                {
                    addMeasureRules(dictionary->get("Measure"), transform);
                }
            }
        }
    }

    if (m_movedPages.empty())
    {
        return;
    }

    // The referenced layout attributes of the structure elements of the moved pages
    const PDFObjectStorage::PDFObjects& objects = m_storage->getObjects();
    for (size_t i = 0; i < objects.size(); ++i)
    {
        const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(objects[i].object);
        if (!dictionary || !dictionary->hasKey("Pg") || !dictionary->get("Pg").isReference())
        {
            continue;
        }
        const auto it = m_movedPages.find(dictionary->get("Pg").getReference());
        if (it == m_movedPages.cend())
        {
            continue;
        }

        const PDFObject& attributes = dictionary->get("A");
        std::vector<PDFObject> items;
        if (attributes.isArray())
        {
            items.assign(attributes.getArray()->begin(), attributes.getArray()->end());
        }
        else
        {
            items.push_back(attributes);
        }
        for (const PDFObject& item : items)
        {
            if (item.isReference())
            {
                Rule rule;
                rule.kind = Rule::Kind::LayoutAttribute;
                rule.transform = it->second;
                m_rules.emplace(item.getReference(), rule);
            }
        }
    }
}

PDFObject PDFPageGeometryEngine::transformAnnotation(const PDFObject& object, const QTransform& transform, bool scale)
{
    const PDFDictionary* dictionary = Helper::getDictionary(object);
    if (!dictionary)
    {
        return object;
    }

    PDFDocumentDataLoaderDecorator loader(m_storage);
    const QRectF rect = loader.readRectangle(dictionary->get("Rect"), QRectF());
    if (!rect.isValid())
    {
        return object;
    }

    // A pop-up and an annotation of the flag NoZoom keep their size (12.5.3, 12.5.6.14); a
    // comment, which is not scaled, moves with the place of the content under it
    const QByteArray subtype = loader.readNameFromDictionary(dictionary, "Subtype");
    const PDFInteger flags = loader.readIntegerFromDictionary(dictionary, "F", 0);
    const bool isScaled = scale && subtype != "Popup" && (flags & Helper::ANNOTATION_NO_ZOOM) == 0;
    QTransform matrix = transform;
    if (!isScaled)
    {
        const QPointF center = rect.center();
        const QPointF moved = transform.map(center);
        matrix = QTransform::fromTranslate(moved.x() - center.x(), moved.y() - center.y());
    }

    std::vector<std::pair<QByteArray, PDFObject>> entries;
    entries.emplace_back("Rect", Helper::createRectangle(matrix.mapRect(rect)));

    // The geometry of the types (Tables 173 to 191): the points move, the appearance stream is
    // mapped to the new /Rect by the algorithm of 12.5.5 (it stays)
    for (const char* key : { "QuadPoints", "Vertices", "L", "CL" })
    {
        if (std::optional<PDFObject> transformed = Helper::transformCoordinates(m_storage, dictionary->get(key), matrix))
        {
            entries.emplace_back(key, std::move(*transformed));
        }
    }
    if (std::optional<PDFObject> transformed = Helper::transformCoordinateLists(m_storage, dictionary->get("InkList"), matrix))
    {
        entries.emplace_back("InkList", std::move(*transformed));
    }
    if (std::optional<PDFObject> transformed = Helper::transformCoordinateLists(m_storage, dictionary->get("Path"), matrix))
    {
        entries.emplace_back("Path", std::move(*transformed));
    }

    if (isScaled)
    {
        const PDFReal scaleX = Helper::getScaleX(matrix);
        const PDFReal scaleY = Helper::getScaleY(matrix);
        const PDFReal lengthScale = Helper::getScale(matrix);

        // /RD: differences of the left, top, right and bottom sides (Tables 180, 181, 183)
        if (std::optional<PDFObject> scaled = Helper::scaleNumbers(m_storage, dictionary->get("RD"), { scaleX, scaleY, scaleX, scaleY }))
        {
            entries.emplace_back("RD", std::move(*scaled));
        }

        // Leader lines of a line (Table 177): lengths perpendicular to the line
        for (const char* key : { "LL", "LLE", "LLO" })
        {
            if (std::optional<PDFObject> scaled = Helper::scaleNumbers(m_storage, dictionary->get(key), { lengthScale }))
            {
                entries.emplace_back(key, std::move(*scaled));
            }
        }

        // A measure of a line, a polygon and a polyline in the annotation itself (12.9.2)
        const PDFObject& measure = dictionary->get("Measure");
        if (measure.isDictionary())
        {
            entries.emplace_back("Measure", transformMeasure(measure, matrix));
        }
    }

    ++m_statistics.annotationsTransformed;
    return Helper::replaceEntries(object, entries);
}

PDFObject PDFPageGeometryEngine::transformNumberFormat(const PDFObject& object, PDFReal factor)
{
    // Table 268: a length of the content, which is \p factor times larger now, is the same real
    // length, so the conversion factor of a unit of the default space is divided by it
    const PDFDictionary* dictionary = Helper::getDictionary(object);
    const std::optional<PDFReal> conversion = dictionary ? Helper::readNumber(m_storage->getObject(dictionary->get("C"))) : std::nullopt;
    if (!conversion || !(factor > 0.0))
    {
        return object;
    }
    return Helper::replaceEntries(object, { { "C", Helper::createNumber(*conversion / factor) } });
}

PDFObject PDFPageGeometryEngine::transformNumberFormats(const PDFObject& formats, PDFReal factor)
{
    // Table 268: /C of the first number format converts the default units to the first unit
    if (!formats.isArray() || formats.getArray()->getCount() == 0 || !(factor > 0.0))
    {
        return formats;
    }

    const PDFObject& first = formats.getArray()->getItem(0);
    if (!first.isDictionary())
    {
        // A referenced format has its own rule
        return formats;
    }

    PDFArrayBuilder array(*formats.getArray());
    array.setItem(transformNumberFormat(first, factor), 0);
    return PDFObject::createArray(std::move(array));
}

PDFObject PDFPageGeometryEngine::transformMeasure(const PDFObject& object, const QTransform& transform)
{
    const PDFDictionary* dictionary = Helper::getDictionary(object);
    if (!dictionary)
    {
        return object;
    }

    // A geospatial measure (12.9.2.3) has only the box of its viewport; a rectilinear one
    // (Table 266) converts lengths of the default space: the factors follow the content
    PDFDocumentDataLoaderDecorator loader(m_storage);
    const QByteArray subtype = loader.readNameFromDictionary(dictionary, "Subtype");
    if (!subtype.isEmpty() && subtype != "RL")
    {
        return object;
    }

    const PDFReal scaleX = Helper::getScaleX(transform);
    const PDFReal scaleY = Helper::getScaleY(transform);
    const PDFReal scale = Helper::getScale(transform);
    const bool hasY = dictionary->hasKey("Y");

    std::vector<std::pair<QByteArray, PDFObject>> entries;
    entries.emplace_back("X", transformNumberFormats(dictionary->get("X"), hasY ? scaleX : scale));
    if (hasY)
    {
        entries.emplace_back("Y", transformNumberFormats(dictionary->get("Y"), scaleY));
    }
    entries.emplace_back("D", transformNumberFormats(dictionary->get("D"), scale));
    entries.emplace_back("A", transformNumberFormats(dictionary->get("A"), scaleX * scaleY));
    if (std::optional<PDFObject> origin = Helper::transformCoordinates(m_storage, dictionary->get("O"), transform))
    {
        entries.emplace_back("O", std::move(*origin));
    }

    // A missing key stays missing (the value of the loop above is the null object, which removes nothing new)
    ++m_statistics.measuresTransformed;
    return Helper::replaceEntries(object, entries);
}

PDFObject PDFPageGeometryEngine::transformViewport(const PDFObject& object, const QTransform& transform)
{
    const PDFDictionary* dictionary = Helper::getDictionary(object);
    if (!dictionary)
    {
        return object;
    }

    PDFDocumentDataLoaderDecorator loader(m_storage);
    std::vector<std::pair<QByteArray, PDFObject>> entries;
    const QRectF box = loader.readRectangle(dictionary->get("BBox"), QRectF());
    if (box.isValid())
    {
        entries.emplace_back("BBox", Helper::createRectangle(transform.mapRect(box)));
    }
    const PDFObject& measure = dictionary->get("Measure");
    if (measure.isDictionary())
    {
        entries.emplace_back("Measure", transformMeasure(measure, transform));
    }

    ++m_statistics.viewportsTransformed;
    return Helper::replaceEntries(object, entries);
}

PDFObject PDFPageGeometryEngine::transformLayoutAttribute(const PDFObject& object, const QTransform& transform, bool* isChanged)
{
    // 14.8.5.4.3: /BBox of a layout attribute is in the default space of the page
    const PDFDictionary* dictionary = Helper::getDictionary(object);
    PDFDocumentDataLoaderDecorator loader(m_storage);
    if (!dictionary || loader.readNameFromDictionary(dictionary, "O") != "Layout")
    {
        return object;
    }
    const QRectF box = loader.readRectangle(dictionary->get("BBox"), QRectF());
    if (!box.isValid())
    {
        return object;
    }

    *isChanged = true;
    ++m_statistics.structureBoxesTransformed;
    return Helper::replaceEntries(object, { { "BBox", Helper::createRectangle(transform.mapRect(box)) } });
}

PDFObject PDFPageGeometryEngine::transformStructureElement(const PDFObject& object, const QTransform& transform)
{
    const PDFDictionary* dictionary = Helper::getDictionary(object);
    if (!dictionary)
    {
        return object;
    }

    // The direct attributes of the element (the referenced ones have their rules)
    const PDFObject& attributes = dictionary->get("A");
    bool isChanged = false;
    PDFObject newAttributes = attributes;
    if (attributes.isDictionary())
    {
        newAttributes = transformLayoutAttribute(attributes, transform, &isChanged);
    }
    else if (attributes.isArray())
    {
        PDFArrayBuilder array(*attributes.getArray());
        for (size_t i = 0; i < array.getCount(); ++i)
        {
            if (array.getItem(i).isDictionary())
            {
                array.setItem(transformLayoutAttribute(array.getItem(i), transform, &isChanged), i);
            }
        }
        newAttributes = PDFObject::createArray(std::move(array));
    }
    return isChanged ? Helper::replaceEntries(object, { { "A", newAttributes } }) : object;
}

PDFObject PDFPageGeometryEngine::applyRule(const PDFObject& object, const Rule& rule)
{
    switch (rule.kind)
    {
        case Rule::Kind::Annotation:
            return transformAnnotation(object, rule.transform, rule.scaleAnnotation);

        case Rule::Kind::Viewport:
            return transformViewport(object, rule.transform);

        case Rule::Kind::Measure:
            return transformMeasure(object, rule.transform);

        case Rule::Kind::NumberFormat:
            return transformNumberFormat(object, rule.factor);

        case Rule::Kind::Bead:
        {
            const PDFDictionary* dictionary = Helper::getDictionary(object);
            PDFDocumentDataLoaderDecorator loader(m_storage);
            const QRectF rect = dictionary ? loader.readRectangle(dictionary->get("R"), QRectF()) : QRectF();
            if (!rect.isValid())
            {
                return object;
            }
            ++m_statistics.beadsTransformed;
            return Helper::replaceEntries(object, { { "R", Helper::createRectangle(rule.transform.mapRect(rect)) } });
        }

        case Rule::Kind::LayoutAttribute:
        {
            bool isChanged = false;
            return transformLayoutAttribute(object, rule.transform, &isChanged);
        }
    }

    return object;
}

PDFObject PDFPageGeometryEngine::createPatternResources(const PDFPage* page, const QTransform& transform)
{
    // 8.7.2: the matrix of a pattern maps to the default space of the page, not to the space of
    // "cm": the patterns, which the content of the page paints with, get the move of the content.
    // Each moved page gets its own resources (the pages, which share them, may move differently).
    const PDFObject& resources = m_storage->getObject(page->getResources());
    const PDFDictionary* resourcesDictionary = Helper::getDictionary(resources);
    const PDFDictionary* patterns = resourcesDictionary ? m_storage->getDictionaryFromObject(resourcesDictionary->get("Pattern")) : nullptr;
    if (!patterns || patterns->isEmpty())
    {
        return PDFObject();
    }

    PDFDocumentDataLoaderDecorator loader(m_storage);
    PDFDictionaryBuilder newPatterns;
    for (const auto& [key, value] : *patterns)
    {
        const PDFObject& pattern = m_storage->getObject(value);
        const PDFDictionary* patternDictionary = Helper::getDictionary(pattern);
        if (!patternDictionary)
        {
            newPatterns.addEntry(key, value);
            continue;
        }

        const QTransform matrix = loader.readMatrixFromDictionary(patternDictionary, "Matrix", QTransform());
        const PDFObject newPattern = Helper::replaceEntries(pattern, { { "Matrix", Helper::createMatrix(matrix * transform) } });
        newPatterns.addEntry(key, PDFObject::createReference(m_builder.addObject(newPattern)));
        ++m_statistics.patternsTransformed;
    }

    return Helper::replaceEntries(resources, { { "Pattern", PDFObject::createDictionary(std::move(newPatterns)) } });
}

PDFObject PDFPageGeometryEngine::transformPage(const PDFObject& object, const PDFPage* page, const PDFPageGeometryChange& change)
{
    std::vector<std::pair<QByteArray, PDFObject>> entries;
    const std::pair<const char*, const std::optional<QRectF>*> boxes[] =
    {
        { "MediaBox", &change.mediaBox },
        { "CropBox", &change.cropBox },
        { "BleedBox", &change.bleedBox },
        { "TrimBox", &change.trimBox },
        { "ArtBox", &change.artBox }
    };
    for (const auto& [key, box] : boxes)
    {
        if (box->has_value())
        {
            entries.emplace_back(key, Helper::createRectangle(**box));
        }
    }

    // 12.3.4: the thumbnail would not match the page
    const PDFDictionary* dictionary = Helper::getDictionary(object);
    if (dictionary && dictionary->hasKey("Thumb"))
    {
        entries.emplace_back("Thumb", PDFObject());
        ++m_statistics.thumbnailsRemoved;
    }

    if (change.hasContentTransform() && dictionary)
    {
        const QTransform& transform = *change.contentTransform;

        // The content: "q matrix cm clip q..." + the streams + "Q..." (R7)
        std::vector<PDFObjectReference> streams = Helper::getContentStreams(&m_builder, m_storage, dictionary->get("Contents"));
        if (!streams.empty())
        {
            const PDFPageGeometryContentBalance balance = Helper::getContentBalance(m_document, page);
            streams.insert(streams.begin(), Helper::addStream(&m_builder, Helper::createContentPrefix(transform, change.contentClip, balance)));
            streams.push_back(Helper::addStream(&m_builder, Helper::createContentSuffix(balance)));
            PDFArrayBuilder contents;
            for (const PDFObjectReference reference : streams)
            {
                contents.appendItem(PDFObject::createReference(reference));
            }
            entries.emplace_back("Contents", PDFObject::createArray(std::move(contents)));
            ++m_statistics.contentsTransformed;
        }

        // The patterns of the resources
        PDFObject resources = createPatternResources(page, transform);
        if (!resources.isNull())
        {
            entries.emplace_back("Resources", std::move(resources));
        }

        // The direct annotations and viewports of the page (the referenced ones have their rules)
        const PDFObject& annotations = dictionary->get("Annots");
        if (annotations.isArray())
        {
            PDFArrayBuilder array(*annotations.getArray());
            bool isChanged = false;
            for (size_t i = 0; i < array.getCount(); ++i)
            {
                if (array.getItem(i).isDictionary())
                {
                    array.setItem(transformAnnotation(array.getItem(i), transform, change.scaleAnnotations), i);
                    isChanged = true;
                }
            }
            if (isChanged)
            {
                entries.emplace_back("Annots", PDFObject::createArray(std::move(array)));
            }
        }
        const PDFObject& viewports = dictionary->get("VP");
        if (viewports.isArray())
        {
            PDFArrayBuilder array(*viewports.getArray());
            bool isChanged = false;
            for (size_t i = 0; i < array.getCount(); ++i)
            {
                if (array.getItem(i).isDictionary())
                {
                    array.setItem(transformViewport(array.getItem(i), transform), i);
                    isChanged = true;
                }
            }
            if (isChanged)
            {
                entries.emplace_back("VP", PDFObject::createArray(std::move(array)));
            }
        }
    }

    ++m_statistics.pagesChanged;
    return Helper::replaceEntries(object, entries);
}

std::optional<QTransform> PDFPageGeometryEngine::getDestinationTransform(const PDFArray* array) const
{
    // 12.3.2.2: [page /XYZ left top zoom], [page /FitH top], ...; a structure destination (PDF 2.0)
    // has the structure element in the place of the page, its page is /Pg of the element
    static const std::set<QByteArray> names = { "XYZ", "Fit", "FitH", "FitV", "FitR", "FitB", "FitBH", "FitBV" };
    if (array->getCount() < 2 || !array->getItem(0).isReference() || !array->getItem(1).isName() ||
        !names.count(array->getItem(1).getString()))
    {
        return std::nullopt;
    }

    const PDFObjectReference target = array->getItem(0).getReference();
    auto it = m_movedPages.find(target);
    if (it != m_movedPages.cend())
    {
        return it->second;
    }

    const PDFDictionary* element = m_storage->getDictionaryFromObject(m_storage->getObjectByReference(target));
    if (element && element->get("Pg").isReference())
    {
        it = m_movedPages.find(element->get("Pg").getReference());
        if (it != m_movedPages.cend())
        {
            return it->second;
        }
    }
    return std::nullopt;
}

PDFObject PDFPageGeometryEngine::transformDestination(const PDFArray* array, const QTransform& transform)
{
    // A coordinate, which is null, keeps its "unchanged" (12.3.2.2)
    PDFArrayBuilder result(*array);
    const QByteArray name = array->getItem(1).getString();
    const bool isDiagonal = Helper::isDiagonal(transform);

    auto mapX = [&](size_t index)
    {
        if (index < array->getCount())
        {
            if (const std::optional<PDFReal> x = Helper::readNumber(array->getItem(index)))
            {
                result.setItem(Helper::createNumber(transform.m11() * *x + transform.dx()), index);
            }
        }
    };
    auto mapY = [&](size_t index)
    {
        if (index < array->getCount())
        {
            if (const std::optional<PDFReal> y = Helper::readNumber(array->getItem(index)))
            {
                result.setItem(Helper::createNumber(transform.m22() * *y + transform.dy()), index);
            }
        }
    };

    if (!isDiagonal || name == "Fit" || name == "FitB")
    {
        return PDFObject::createArray(PDFArrayBuilder(*array));
    }

    if (name == "XYZ")
    {
        mapX(2);
        mapY(3);
    }
    else if (name == "FitH" || name == "FitBH")
    {
        mapY(2);
    }
    else if (name == "FitV" || name == "FitBV")
    {
        mapX(2);
    }
    else if (name == "FitR")
    {
        mapX(2);
        mapY(3);
        mapX(4);
        mapY(5);
    }

    ++m_statistics.destinationsTransformed;
    return PDFObject::createArray(std::move(result));
}

PDFObject PDFPageGeometryEngine::transformDestinations(const PDFObject& object, int depth, bool* isChanged)
{
    if (depth > Helper::MAXIMUM_DEPTH)
    {
        return object;
    }

    if (object.isArray())
    {
        const PDFArray* array = object.getArray();
        if (const std::optional<QTransform> transform = getDestinationTransform(array))
        {
            const QByteArray name = array->getItem(1).getString();
            if (name == "Fit" || name == "FitB")
            {
                return object;
            }
            *isChanged = true;
            return transformDestination(array, *transform);
        }

        std::optional<PDFArrayBuilder> builder;
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            const PDFObject& item = array->getItem(i);
            if (!item.isArray() && !item.isDictionary())
            {
                continue;
            }
            bool isItemChanged = false;
            PDFObject newItem = transformDestinations(item, depth + 1, &isItemChanged);
            if (isItemChanged)
            {
                if (!builder)
                {
                    builder.emplace(*array);
                }
                builder->setItem(std::move(newItem), i);
            }
        }
        if (builder)
        {
            *isChanged = true;
            return PDFObject::createArray(std::move(*builder));
        }
        return object;
    }

    if (object.isDictionary())
    {
        const PDFDictionary* dictionary = object.getDictionary();
        std::vector<std::pair<QByteArray, PDFObject>> entries;
        for (const auto& [key, value] : *dictionary)
        {
            if (!value.isArray() && !value.isDictionary())
            {
                continue;
            }
            bool isItemChanged = false;
            PDFObject newValue = transformDestinations(value, depth + 1, &isItemChanged);
            if (isItemChanged)
            {
                entries.emplace_back(key.getString(), std::move(newValue));
            }
        }
        if (!entries.empty())
        {
            *isChanged = true;
            return Helper::replaceEntries(object, entries);
        }
    }

    return object;
}

PDFDocument PDFPageGeometryEngine::run(PDFPageGeometryStatistics* statistics)
{
    collectRules();

    // One pass over every object of the file: no walk of the graph, so a cycle or a shared node
    // cannot repeat a change; an object changes at most once by its rule, once as a page and
    // once by the destinations in it
    const PDFObjectStorage::PDFObjects& objects = m_storage->getObjects();
    for (size_t i = 0; i < objects.size(); ++i)
    {
        if (i % 1024 == 0 && PDFOperationControl::isOperationCancelled(m_control))
        {
            if (statistics)
            {
                *statistics = PDFPageGeometryStatistics();
            }
            return PDFDocument();
        }

        const PDFObjectStorage::Entry& entry = objects[i];
        if (entry.object.isNull())
        {
            continue;
        }

        const PDFObjectReference reference(PDFInteger(i), entry.generation);
        PDFObject object = entry.object;
        bool isChanged = false;

        if (const auto rule = m_rules.find(reference); rule != m_rules.cend())
        {
            object = applyRule(object, rule->second);
            isChanged = true;
        }
        if (const auto change = m_changes.find(reference); change != m_changes.cend())
        {
            object = transformPage(object, m_pages.at(reference), *change->second);
            isChanged = true;
        }
        if (!m_movedPages.empty())
        {
            if (const PDFDictionary* dictionary = Helper::getDictionary(object); dictionary && dictionary->get("Pg").isReference())
            {
                const auto moved = m_movedPages.find(dictionary->get("Pg").getReference());
                if (moved != m_movedPages.cend())
                {
                    PDFObject element = transformStructureElement(object, moved->second);
                    isChanged = isChanged || !(element == object);
                    object = std::move(element);
                }
            }
            if (object.isDictionary() || object.isArray())
            {
                object = transformDestinations(object, 0, &isChanged);
            }
        }

        if (isChanged)
        {
            m_builder.setObject(reference, std::move(object));
        }
    }

    PDFDocument built = m_builder.build();
    PDFDocument result(PDFObjectStorage(built.getStorage()), m_document->getInfo()->version, m_document->getSourceDataHash());

    // The own OCR layers of the pages stay bound to them (the text layer is a part of the content
    // and it moves with it)
    std::vector<std::pair<PDFInteger, PDFInteger>> pages;
    for (const auto& [reference, change] : m_changes)
    {
        pages.emplace_back(change->pageIndex, change->pageIndex);
    }
    PDFDocumentBuilder rebindBuilder(&result);
    if (!PDFOCRTextLayerWriter::rebindFingerprintsOfPages(&rebindBuilder, m_document, &result, pages).empty())
    {
        PDFDocument rebound = rebindBuilder.build();
        result = PDFDocument(PDFObjectStorage(rebound.getStorage()), m_document->getInfo()->version, m_document->getSourceDataHash());
    }

    if (statistics)
    {
        *statistics = m_statistics;
    }
    return result;
}

// ------------------------------------------------------------------- PDFPageGeometry

QRectF PDFPageGeometry::getEffectiveBox(const PDFPage* page, Box box)
{
    switch (box)
    {
        case Box::MediaBox:
            return page->getMediaBox().normalized();
        case Box::CropBox:
            return page->getEffectiveCropBox();
        case Box::BleedBox:
            return page->getEffectiveBleedBox();
        case Box::TrimBox:
            return page->getEffectiveTrimBox();
        case Box::ArtBox:
            return page->getEffectiveArtBox();
    }
    return page->getMediaBox().normalized();
}

bool PDFPageGeometry::isBoxDefined(const PDFPage* page, const PDFObjectStorage* storage, Box box)
{
    if (box == Box::MediaBox)
    {
        return true;
    }

    static constexpr const char* keys[] = { "MediaBox", "CropBox", "BleedBox", "TrimBox", "ArtBox" };
    const char* key = keys[int(box)];
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(page->getPageReference()));
    if (!dictionary)
    {
        return false;
    }
    if (dictionary->hasKey(key))
    {
        return true;
    }
    if (box != Box::CropBox)
    {
        // Table 31: only the media box and the crop box are inherited
        return false;
    }

    // The crop box of an ancestor (one set of the visited nodes, a depth limit)
    std::set<PDFObjectReference> visited = { page->getPageReference() };
    PDFObject parent = dictionary->get("Parent");
    for (int depth = 0; depth < PDFPageGeometryHelper::MAXIMUM_DEPTH && parent.isReference(); ++depth)
    {
        if (!visited.insert(parent.getReference()).second)
        {
            return false;
        }
        const PDFDictionary* node = storage->getDictionaryFromObject(storage->getObjectByReference(parent.getReference()));
        if (!node)
        {
            return false;
        }
        if (node->hasKey(key))
        {
            return true;
        }
        parent = node->get("Parent");
    }
    return false;
}

QTransform PDFPageGeometry::getShownToUserTransform(const QRectF& box, PageRotation rotation)
{
    const QRectF normalized = box.normalized();
    const QRectF rotatedBox = PDFPage::getRotatedBox(normalized, rotation);
    if (rotatedBox.width() <= 0.0 || rotatedBox.height() <= 0.0)
    {
        return QTransform();
    }
    const QTransform userToShown = PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedBox, QRectF(QPointF(0.0, 0.0), rotatedBox.size()), rotation);
    return userToShown.inverted();
}

QMarginsF PDFPageGeometry::getUnrotatedMargins(const QMarginsF& shownMargins, PageRotation rotation)
{
    // A box of 1000 x 1000 shows its margins at the sides, which the rotation gives
    const QRectF box(0.0, 0.0, 1000.0, 1000.0);
    const QTransform transform = getShownToUserTransform(box, rotation);
    const QRectF inner = transform.mapRect(QRectF(shownMargins.left(), shownMargins.top(),
                                                  box.width() - shownMargins.left() - shownMargins.right(),
                                                  box.height() - shownMargins.top() - shownMargins.bottom()));
    // QRectF::top is the smallest y: the bottom of the page in the default user space
    return QMarginsF(inner.left() - box.left(), box.bottom() - inner.bottom(), box.right() - inner.right(), inner.top() - box.top());
}

bool PDFPageGeometry::isValidBox(const QRectF& box)
{
    const QRectF normalized = box.normalized();
    for (const PDFReal value : { normalized.left(), normalized.top(), normalized.width(), normalized.height() })
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return normalized.width() >= MINIMUM_SIDE && normalized.width() <= MAXIMUM_SIDE &&
           normalized.height() >= MINIMUM_SIDE && normalized.height() <= MAXIMUM_SIDE;
}

QString PDFPageGeometry::getBoxName(Box box)
{
    switch (box)
    {
        case Box::MediaBox:
            return QStringLiteral("media");
        case Box::CropBox:
            return QStringLiteral("crop");
        case Box::BleedBox:
            return QStringLiteral("bleed");
        case Box::TrimBox:
            return QStringLiteral("trim");
        case Box::ArtBox:
            return QStringLiteral("art");
    }
    return QString();
}

std::optional<PDFPageGeometry::Box> PDFPageGeometry::getBoxByName(const QString& name)
{
    for (const Box box : getAllBoxes())
    {
        if (getBoxName(box) == name)
        {
            return box;
        }
    }
    return std::nullopt;
}

std::vector<PDFPageGeometry::Box> PDFPageGeometry::getAllBoxes()
{
    return { Box::MediaBox, Box::CropBox, Box::BleedBox, Box::TrimBox, Box::ArtBox };
}

QSizeF PDFPageGeometry::getShownSizeMM(const PDFPage* page)
{
    const QSizeF size = page->getRotatedEffectiveCropBox().size();
    const PDFReal factor = page->getMillimetersToUnits();
    return QSizeF(size.width() / factor, size.height() / factor);
}

std::optional<QRectF> PDFPageGeometry::computeCropBox(const PDFPage* page, const PDFPage* referencePage, const CropSettings& settings)
{
    if (!page)
    {
        return std::nullopt;
    }

    const QRectF visible = page->getEffectiveCropBox();
    const PDFReal factor = page->getMillimetersToUnits();
    QMarginsF shownMargins;
    switch (settings.mode)
    {
        case CropMode::Remove:
            // The crop box is written: a missing key could inherit the crop box of an ancestor
            return page->getMediaBox().normalized();

        case CropMode::EqualMargins:
        {
            const PDFReal margin = settings.marginMM * factor;
            shownMargins = QMarginsF(margin, margin, margin, margin);
            break;
        }

        case CropMode::CustomBox:
        {
            // The margins of the box on the reference page (the page of the preview)
            const PDFPage* reference = referencePage ? referencePage : page;
            const QSizeF referenceSize = getShownSizeMM(reference);
            const QRectF& box = settings.boxMM;
            shownMargins = QMarginsF(box.left() * factor,
                                     box.top() * factor,
                                     (referenceSize.width() - box.left() - box.width()) * factor,
                                     (referenceSize.height() - box.top() - box.height()) * factor);
            break;
        }
    }

    for (const PDFReal value : { shownMargins.left(), shownMargins.top(), shownMargins.right(), shownMargins.bottom() })
    {
        if (!std::isfinite(value) || value < 0.0)
        {
            return std::nullopt;
        }
    }

    const QSizeF shownSize = page->getRotatedEffectiveCropBox().size();
    const QRectF shownInner(shownMargins.left(), shownMargins.top(),
                            shownSize.width() - shownMargins.left() - shownMargins.right(),
                            shownSize.height() - shownMargins.top() - shownMargins.bottom());
    if (shownInner.width() < MINIMUM_SIDE || shownInner.height() < MINIMUM_SIDE)
    {
        return std::nullopt;
    }

    return getShownToUserTransform(visible, page->getPageRotation()).mapRect(shownInner).normalized();
}

PDFPageGeometry::ResizePlacement PDFPageGeometry::computeResize(const PDFPage* page, const ResizeSettings& settings, const PDFObjectStorage* storage)
{
    ResizePlacement placement;
    if (!page)
    {
        placement.errorMessage = tr("There is no page.");
        return placement;
    }

    const QRectF visible = page->getEffectiveCropBox();
    const PageRotation rotation = page->getPageRotation();
    const QSizeF shownSource = PDFPage::getRotatedBox(visible, rotation).size();
    const PDFReal factor = page->getMillimetersToUnits();

    // The sheet as shown
    QSizeF sheet = shownSource;
    if (settings.pageSizeMM)
    {
        sheet = QSizeF(settings.pageSizeMM->width() * factor, settings.pageSizeMM->height() * factor);
        if (!settings.isCustomSize)
        {
            const bool isSheetLandscape = sheet.width() > sheet.height();
            bool isLandscape = isSheetLandscape;
            switch (settings.orientation)
            {
                case Orientation::Auto:
                    isLandscape = shownSource.width() > shownSource.height();
                    break;
                case Orientation::Portrait:
                    isLandscape = false;
                    break;
                case Orientation::Landscape:
                    isLandscape = true;
                    break;
            }
            if (isLandscape != isSheetLandscape)
            {
                sheet.transpose();
            }
        }
    }
    if (!isValidBox(QRectF(QPointF(0.0, 0.0), sheet)))
    {
        placement.errorMessage = tr("The page size is outside of the limits of a page (3 to 14,400 units).");
        return placement;
    }

    const QMarginsF margins = settings.marginsMM * factor;
    for (const PDFReal value : { margins.left(), margins.top(), margins.right(), margins.bottom(), settings.offsetMM.x(), settings.offsetMM.y() })
    {
        if (!std::isfinite(value))
        {
            placement.errorMessage = tr("A margin or an offset is not a number.");
            return placement;
        }
    }

    const QRectF inner(margins.left(), margins.top(), sheet.width() - margins.left() - margins.right(), sheet.height() - margins.top() - margins.bottom());
    placement.sheetSize = sheet;
    placement.innerRect = inner;
    if (!(inner.width() > 0.0 && inner.height() > 0.0))
    {
        placement.errorMessage = tr("The margins exceed the page size.");
        return placement;
    }

    PDFReal scaleX = 1.0;
    PDFReal scaleY = 1.0;
    if (settings.scaleContent)
    {
        scaleX = inner.width() / shownSource.width();
        scaleY = inner.height() / shownSource.height();
        if (settings.preserveAspectRatio)
        {
            scaleX = scaleY = qMin(scaleX, scaleY);
        }
    }

    const QSizeF contentSize(shownSource.width() * scaleX, shownSource.height() * scaleY);
    PDFReal x = inner.left() + (inner.width() - contentSize.width()) * 0.5;
    if (settings.anchor.testFlag(Qt::AlignLeft))
    {
        x = inner.left();
    }
    else if (settings.anchor.testFlag(Qt::AlignRight))
    {
        x = inner.right() - contentSize.width();
    }
    PDFReal y = inner.top() + (inner.height() - contentSize.height()) * 0.5;
    if (settings.anchor.testFlag(Qt::AlignTop))
    {
        y = inner.top();
    }
    else if (settings.anchor.testFlag(Qt::AlignBottom))
    {
        y = inner.bottom() - contentSize.height();
    }
    x += settings.offsetMM.x() * factor;
    y += settings.offsetMM.y() * factor;

    placement.contentRect = QRectF(QPointF(x, y), contentSize);
    placement.scaleX = scaleX;
    placement.scaleY = scaleY;

    // The matrix of the content: the shown source to the shown sheet, in the default spaces
    const QSizeF sheetUnrotated = PDFPage::getRotatedSize(sheet, rotation);
    const QRectF sheetBox(QPointF(0.0, 0.0), sheetUnrotated);
    const QTransform sourceToShown = getShownToUserTransform(visible, rotation).inverted();
    const QTransform placementMatrix(scaleX, 0.0, 0.0, scaleY, x, y);
    const QTransform contentTransform = sourceToShown * placementMatrix * getShownToUserTransform(sheetBox, rotation);

    PDFPageGeometryChange& change = placement.change;
    const bool isCropOfMedia = visible == page->getMediaBox().normalized();
    if (settings.scaleContent)
    {
        // The sheet is the new page; what the crop box hid stays hidden by the clip
        change.mediaBox = sheetBox;
        change.cropBox = sheetBox;
        change.contentTransform = contentTransform;
        change.contentClip = isCropOfMedia ? QRectF() : visible;
        change.scaleAnnotations = settings.scaleAnnotations;
        for (const auto& [box, target] : { std::pair<Box, std::optional<QRectF>*>(Box::BleedBox, &change.bleedBox),
                                           std::pair<Box, std::optional<QRectF>*>(Box::TrimBox, &change.trimBox),
                                           std::pair<Box, std::optional<QRectF>*>(Box::ArtBox, &change.artBox) })
        {
            // A box, which the page does not have (its default is the crop box), follows the new crop box
            const QRectF value = getEffectiveBox(page, box);
            if (storage ? isBoxDefined(page, storage, box) : value != visible)
            {
                *target = PDFPage::getEffectiveBox(contentTransform.mapRect(value), sheetBox);
            }
        }
    }
    else
    {
        // Keep size: nothing is rewritten, the media box moves around the content (391:24594)
        const QRectF media = contentTransform.inverted().mapRect(sheetBox);
        change.mediaBox = media;
        change.cropBox = isCropOfMedia ? media : PDFPage::getEffectiveBox(visible, media);
        for (const auto& [box, target] : { std::pair<Box, std::optional<QRectF>*>(Box::BleedBox, &change.bleedBox),
                                           std::pair<Box, std::optional<QRectF>*>(Box::TrimBox, &change.trimBox),
                                           std::pair<Box, std::optional<QRectF>*>(Box::ArtBox, &change.artBox) })
        {
            const QRectF value = getEffectiveBox(page, box);
            if (storage ? isBoxDefined(page, storage, box) : value != visible)
            {
                *target = PDFPage::getEffectiveBox(value, media);
            }
        }
    }

    placement.isValid = true;
    return placement;
}

QRectF PDFPageGeometry::computeBox(const PDFPage* page, Box basedOn, const QMarginsF& marginsMM)
{
    if (!page)
    {
        return QRectF();
    }

    const QRectF box = getEffectiveBox(page, basedOn);
    const PDFReal factor = page->getMillimetersToUnits();
    const QMarginsF margins = marginsMM * factor;
    const QSizeF shown = PDFPage::getRotatedBox(box, page->getPageRotation()).size();
    const QRectF shownBox(-margins.left(), -margins.top(), shown.width() + margins.left() + margins.right(), shown.height() + margins.top() + margins.bottom());
    if (!(shownBox.width() > 0.0 && shownBox.height() > 0.0))
    {
        return QRectF();
    }
    return getShownToUserTransform(box, page->getPageRotation()).mapRect(shownBox).normalized();
}

PDFDocument PDFPageGeometry::applyChanges(const PDFDocument* document,
                                          const std::vector<PDFPageGeometryChange>& changes,
                                          const PDFOperationControl* control,
                                          PDFPageGeometryStatistics* statistics)
{
    if (!document)
    {
        throw PDFException(tr("There is no document."));
    }

    PDFPageGeometryEngine engine(document, changes, control);
    return engine.run(statistics);
}

bool PDFPageGeometry::isAnnotationChanged(const PDFDocument* document, const std::vector<PDFPageGeometryChange>& changes)
{
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!catalog)
    {
        return false;
    }
    for (const PDFPageGeometryChange& change : changes)
    {
        if (change.hasContentTransform() && change.pageIndex >= 0 && size_t(change.pageIndex) < catalog->getPageCount() &&
            !catalog->getPage(size_t(change.pageIndex))->getAnnotations().empty())
        {
            return true;
        }
    }
    return false;
}

// --------------------------------------------- the dialog Page Geometry (Editor, PageMaster)

/// The pages of the settings of the dialog Page Geometry
class PDFPageGeometrySettingsHelper
{
public:
    PDFPageGeometrySettingsHelper() = delete;

    static Qt::Alignment getAnchorAlignment(PDFPageGeometrySettings::Anchor anchor)
    {
        switch (anchor)
        {
            case PDFPageGeometrySettings::Anchor::TopLeft:
                return Qt::AlignTop | Qt::AlignLeft;
            case PDFPageGeometrySettings::Anchor::TopCenter:
                return Qt::AlignTop | Qt::AlignHCenter;
            case PDFPageGeometrySettings::Anchor::TopRight:
                return Qt::AlignTop | Qt::AlignRight;
            case PDFPageGeometrySettings::Anchor::MiddleLeft:
                return Qt::AlignVCenter | Qt::AlignLeft;
            case PDFPageGeometrySettings::Anchor::MiddleCenter:
                return Qt::AlignCenter;
            case PDFPageGeometrySettings::Anchor::MiddleRight:
                return Qt::AlignVCenter | Qt::AlignRight;
            case PDFPageGeometrySettings::Anchor::BottomLeft:
                return Qt::AlignBottom | Qt::AlignLeft;
            case PDFPageGeometrySettings::Anchor::BottomCenter:
                return Qt::AlignBottom | Qt::AlignHCenter;
            case PDFPageGeometrySettings::Anchor::BottomRight:
                return Qt::AlignBottom | Qt::AlignRight;
        }
        return Qt::AlignCenter;
    }

    static std::vector<PDFInteger> selectPageIndices(const PDFDocument* document, const PDFPageGeometrySettings& settings, QString* errorMessage)
    {
        std::vector<PDFInteger> result;
        const PDFCatalog* catalog = document->getCatalog();
        const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
        if (pageCount <= 0)
        {
            return result;
        }

        // B46: a range outside of the document is an error, not "no page"; an export of PageMaster
        // applies the settings to each of its outputs (B27): the pages beyond an output are left out
        std::optional<std::set<PDFInteger>> rangeSelection;
        const QString rangeText = settings.pageRange.simplified();
        if (!rangeText.isEmpty())
        {
            QString parseError;
            PDFClosedIntervalSet closedIntervalSet = PDFClosedIntervalSet::parse(1, pageCount, rangeText, &parseError);
            if (!parseError.isEmpty())
            {
                *errorMessage = parseError;
                return { };
            }
            // The range is checked before it is unfolded (a hostile "1-1000000000" is no list)
            if (settings.isRangeLimitedToDocument)
            {
                closedIntervalSet.intersect(1, pageCount);
            }
            PDFClosedIntervalSet withDocument = closedIntervalSet;
            withDocument.addInterval(1, pageCount);
            if (!withDocument.isCovered(1, pageCount))
            {
                *errorMessage = PDFPageGeometry::tr("The page range %1 is outside of the document – it has %Ln pages.", nullptr, int(qMin<PDFInteger>(pageCount, std::numeric_limits<int>::max()))).arg(rangeText);
                return { };
            }
            rangeSelection.emplace();
            for (const PDFInteger pageNumber : closedIntervalSet.unfold())
            {
                rangeSelection->insert(pageNumber);
            }
        }

        for (PDFInteger pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        {
            const PDFInteger pageNumber = pageIndex + 1;
            if (rangeSelection && !rangeSelection->count(pageNumber))
            {
                continue;
            }

            const PDFPage* page = catalog->getPage(size_t(pageIndex));
            const QSizeF shownSize = PDFPage::getRotatedBox(PDFPageGeometry::getEffectiveBox(page, settings.referenceBox), page->getPageRotation()).size();
            switch (settings.pageSubset)
            {
                case PDFPageGeometrySettings::PageSubset::AllPages:
                    break;
                case PDFPageGeometrySettings::PageSubset::OddPages:
                    if (pageNumber % 2 == 0)
                    {
                        continue;
                    }
                    break;
                case PDFPageGeometrySettings::PageSubset::EvenPages:
                    if (pageNumber % 2 != 0)
                    {
                        continue;
                    }
                    break;
                case PDFPageGeometrySettings::PageSubset::PortraitPages:
                    if (shownSize.width() > shownSize.height())
                    {
                        continue;
                    }
                    break;
                case PDFPageGeometrySettings::PageSubset::LandscapePages:
                    if (shownSize.width() < shownSize.height())
                    {
                        continue;
                    }
                    break;
            }
            result.push_back(pageIndex);
        }
        return result;
    }

    /// The change of a page by the settings of the dialog
    static std::optional<PDFPageGeometryChange> createChange(const PDFPage* page, PDFInteger pageIndex, const PDFPageGeometrySettings& settings, const PDFObjectStorage* storage, QString* errorMessage)
    {
        using Box = PDFPageGeometry::Box;

        const QRectF source = PDFPageGeometry::getEffectiveBox(page, settings.referenceBox);
        const PageRotation rotation = page->getPageRotation();
        const QSizeF shownSource = PDFPage::getRotatedBox(source, rotation).size();
        if (!(shownSource.width() > 0.0 && shownSource.height() > 0.0))
        {
            *errorMessage = PDFPageGeometry::tr("Reference box on page %1 is invalid.").arg(pageIndex + 1);
            return std::nullopt;
        }

        // B41, B42: millimeters by /UserUnit, the sizes, margins and anchor as the page is shown
        const PDFReal factor = page->getMillimetersToUnits();
        const QMarginsF margins = settings.marginsMM * factor;
        QSizeF sheet(shownSource.width() + margins.left() + margins.right(), shownSource.height() + margins.top() + margins.bottom());
        if (settings.useTargetPageSize)
        {
            sheet = settings.targetPageSizeMM * factor;
        }
        if (!PDFPageGeometry::isValidBox(QRectF(QPointF(0.0, 0.0), sheet)))
        {
            *errorMessage = PDFPageGeometry::tr("Target page size for page %1 is invalid.").arg(pageIndex + 1);
            return std::nullopt;
        }

        const QRectF inner(margins.left(), margins.top(), sheet.width() - margins.left() - margins.right(), sheet.height() - margins.top() - margins.bottom());
        if (!(inner.width() > 0.0 && inner.height() > 0.0))
        {
            *errorMessage = PDFPageGeometry::tr("Margins for page %1 exceed target page size.").arg(pageIndex + 1);
            return std::nullopt;
        }

        PDFReal scaleX = 1.0;
        PDFReal scaleY = 1.0;
        if (settings.scaleContent)
        {
            scaleX = inner.width() / shownSource.width();
            scaleY = inner.height() / shownSource.height();
            if (settings.preserveAspectRatio)
            {
                scaleX = scaleY = qMin(scaleX, scaleY);
            }
        }

        const QSizeF contentSize(shownSource.width() * scaleX, shownSource.height() * scaleY);
        const Qt::Alignment anchor = getAnchorAlignment(settings.anchor);
        PDFReal x = inner.left() + (inner.width() - contentSize.width()) * 0.5;
        if (anchor.testFlag(Qt::AlignLeft))
        {
            x = inner.left();
        }
        else if (anchor.testFlag(Qt::AlignRight))
        {
            x = inner.right() - contentSize.width();
        }
        PDFReal y = inner.top() + (inner.height() - contentSize.height()) * 0.5;
        if (anchor.testFlag(Qt::AlignTop))
        {
            y = inner.top();
        }
        else if (anchor.testFlag(Qt::AlignBottom))
        {
            y = inner.bottom() - contentSize.height();
        }

        // The offset of the dialog points up (as the default user space of an unrotated page)
        x += settings.offsetMM.x() * factor;
        y -= settings.offsetMM.y() * factor;

        const QSizeF sheetUnrotated = PDFPage::getRotatedSize(sheet, rotation);
        const QRectF sheetBox(QPointF(0.0, 0.0), sheetUnrotated);
        const QTransform contentTransform = PDFPageGeometry::getShownToUserTransform(source, rotation).inverted() *
                                            QTransform(scaleX, 0.0, 0.0, scaleY, x, y) *
                                            PDFPageGeometry::getShownToUserTransform(sheetBox, rotation);

        // B43, B44: the selected boxes become the sheet; with the content scaled the other boxes,
        // which the page has, follow the content, without it they stay; nothing is outside of the media box
        PDFPageGeometryChange change;
        change.pageIndex = pageIndex;
        const QRectF sheetInUserSpace = settings.scaleContent ? sheetBox : contentTransform.inverted().mapRect(sheetBox);
        if (settings.scaleContent)
        {
            change.contentTransform = contentTransform;
            change.scaleAnnotations = settings.scaleAnnotationsAndFormFields;
        }

        const QRectF media = settings.applyMediaBox ? sheetInUserSpace : (settings.scaleContent ? contentTransform.mapRect(page->getMediaBox().normalized()) : page->getMediaBox().normalized());
        change.mediaBox = media;

        const std::tuple<Box, bool, std::optional<QRectF>*> boxes[] =
        {
            { Box::CropBox, settings.applyCropBox, &change.cropBox },
            { Box::BleedBox, settings.applyBleedBox, &change.bleedBox },
            { Box::TrimBox, settings.applyTrimBox, &change.trimBox },
            { Box::ArtBox, settings.applyArtBox, &change.artBox }
        };
        for (const auto& [box, isApplied, target] : boxes)
        {
            if (isApplied)
            {
                *target = PDFPage::getEffectiveBox(sheetInUserSpace, media);
                continue;
            }

            // The crop box is always written (an inherited one would stay the old one);
            // the bleed, trim and art boxes only when the page has them
            const QRectF value = PDFPageGeometry::getEffectiveBox(page, box);
            if (box == Box::CropBox || PDFPageGeometry::isBoxDefined(page, storage, box))
            {
                *target = PDFPage::getEffectiveBox(settings.scaleContent ? contentTransform.mapRect(value) : value, media);
            }
        }
        return change;
    }
};

PDFOperationResult PDFPageGeometry::apply(PDFDocument* document,
                                          const PDFPageGeometrySettings& settings,
                                          PDFModifiedDocument::ModificationFlags* modificationFlags)
{
    if (modificationFlags)
    {
        *modificationFlags = PDFModifiedDocument::ModificationFlags();
    }

    if (!document)
    {
        return tr("Invalid document.");
    }

    if (!settings.hasAnyTargetBoxSelected())
    {
        return tr("No target page box selected.");
    }

    QString errorMessage;
    const std::vector<PDFInteger> pageIndices = PDFPageGeometrySettingsHelper::selectPageIndices(document, settings, &errorMessage);
    if (!errorMessage.isEmpty())
    {
        return errorMessage;
    }
    if (pageIndices.empty())
    {
        return true;
    }

    std::vector<PDFPageGeometryChange> changes;
    changes.reserve(pageIndices.size());
    const PDFCatalog* catalog = document->getCatalog();
    for (const PDFInteger pageIndex : pageIndices)
    {
        std::optional<PDFPageGeometryChange> change = PDFPageGeometrySettingsHelper::createChange(catalog->getPage(size_t(pageIndex)), pageIndex, settings, &document->getStorage(), &errorMessage);
        if (!change)
        {
            return errorMessage;
        }
        changes.push_back(std::move(*change));
    }

    try
    {
        PDFPageGeometryStatistics statistics;
        PDFDocument result = applyChanges(document, changes, nullptr, &statistics);
        *document = std::move(result);

        PDFModifiedDocument::ModificationFlags flags = PDFModifiedDocument::ModificationFlags(PDFModifiedDocument::Reset | PDFModifiedDocument::PreserveUndoRedo);
        if (statistics.contentsTransformed > 0)
        {
            flags |= PDFModifiedDocument::PageContents;
        }
        if (statistics.annotationsTransformed > 0)
        {
            flags |= PDFModifiedDocument::Annotation;
        }
        if (modificationFlags)
        {
            *modificationFlags = flags;
        }
    }
    catch (const PDFException& exception)
    {
        return exception.getMessage();
    }

    return true;
}

}   // namespace pdf
