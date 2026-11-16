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

#include "pdfdocumenttool_redact.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumenttextindex.h"
#include "pdfdocumenttool_merge.h"
#include "pdffont.h"
#include "pdfform.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfprogress.h"
#include "pdfredact.h"
#include "pdfredactionverifier.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace pdf
{

/// The parameters of Redact Content as values (PDFDocumentToolRedact)
class PDFDocumentToolRedactHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolRedact)

public:
    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// One area of the parameter areas
    struct AreaEntry
    {
        PDFInteger pageIndex = -1;  ///< The page (index from 0)
        QRectF rect;                ///< The rectangle in the default user space of the page
    };

    /// The areas of the JSON [{ "page": n from 1, "rect": [x, y, width, height] }] (untrusted input of
    /// MCP: exactly these keys, a page number, four finite numbers, a positive size, at most
    /// MAXIMUM_AREA_COUNT areas); std::nullopt and the reason for a wrong value. An empty text is no area.
    static std::optional<std::vector<AreaEntry>> parseAreas(const QString& text, QString* errorMessage)
    {
        std::vector<AreaEntry> areas;
        if (text.trimmed().isEmpty())
        {
            return areas;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray())
        {
            *errorMessage = tr("The areas are not a JSON array of objects { \"page\": 1, \"rect\": [x, y, width, height] }.");
            return std::nullopt;
        }

        const QJsonArray array = document.array();
        if (array.size() > PDFDocumentToolRedact::MAXIMUM_AREA_COUNT)
        {
            *errorMessage = tr("At most %1 areas can be redacted at once.").arg(PDFDocumentToolRedact::MAXIMUM_AREA_COUNT);
            return std::nullopt;
        }

        for (qsizetype i = 0; i < array.size(); ++i)
        {
            const QJsonObject object = array.at(i).toObject();
            const QJsonValue page = object.value(QStringLiteral("page"));
            const QJsonArray rect = object.value(QStringLiteral("rect")).toArray();
            const bool hasOnlyKnownKeys = object.size() == 2 && object.contains(QStringLiteral("page")) && object.contains(QStringLiteral("rect"));
            const double pageNumber = page.toDouble(-1.0);
            bool isValid = array.at(i).isObject() && hasOnlyKnownKeys && page.isDouble() && pageNumber >= 1.0 && pageNumber == std::floor(pageNumber) &&
                           pageNumber <= double(PDFDocumentToolPageSelection::MAXIMUM_PAGE_NUMBER) && rect.size() == 4;
            std::array<double, 4> values = { };
            for (qsizetype j = 0; isValid && j < 4; ++j)
            {
                isValid = rect.at(j).isDouble() && std::isfinite(rect.at(j).toDouble());
                values[size_t(j)] = rect.at(j).toDouble();
            }
            isValid = isValid && values[2] > 0.0 && values[3] > 0.0;
            if (!isValid)
            {
                *errorMessage = tr("Area %1 is not valid: it needs \"page\" (a page number from 1) and \"rect\" ([x, y, width, height] with a positive width and height).").arg(i + 1);
                return std::nullopt;
            }

            areas.push_back({ PDFInteger(pageNumber) - 1, QRectF(values[0], values[1], values[2], values[3]) });
        }
        return areas;
    }

    /// The JSON of the areas (parseAreas reads it back)
    static QString createAreasText(const std::vector<AreaEntry>& areas)
    {
        QJsonArray array;
        for (const AreaEntry& area : areas)
        {
            QJsonObject object;
            object.insert(QStringLiteral("page"), qint64(area.pageIndex + 1));
            object.insert(QStringLiteral("rect"), QJsonArray({ area.rect.x(), area.rect.y(), area.rect.width(), area.rect.height() }));
            array.append(object);
        }
        return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    }

    /// The options of the search of the parameters
    static PDFDocumentTextSearchOptions getSearchOptions(const QVariantMap& parameters)
    {
        PDFDocumentTextSearchOptions options;
        options.caseSensitive = parameters.value(name(PDFDocumentToolRedact::PARAMETER_SEARCH_CASE_SENSITIVE)).toBool();
        options.wholeWords = parameters.value(name(PDFDocumentToolRedact::PARAMETER_SEARCH_WHOLE_WORDS)).toBool();
        options.regularExpression = parameters.value(name(PDFDocumentToolRedact::PARAMETER_SEARCH_REGEX)).toBool();
        return options;
    }

    /// The whole pages of the parameter pages (empty for no page)
    static std::vector<PDFInteger> getWholePages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
    {
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters.value(name(PDFDocumentToolRedact::PARAMETER_PAGES)), errorMessage);
        if (!selection || selection->isNone)
        {
            return { };
        }
        return selection->getPageIndices(document, errorMessage);
    }

    /// The name of the fill for the facts (437:13887 "Black")
    static QString getFillName(const std::optional<QColor>& fillColor)
    {
        if (!fillColor)
        {
            return tr("No fill");
        }
        if (fillColor->rgb() == qRgb(0, 0, 0))
        {
            return tr("Black");
        }
        if (fillColor->rgb() == qRgb(255, 255, 255))
        {
            return tr("White");
        }
        return fillColor->name(QColor::HexRgb).toUpper();
    }

    /// The fill of the parameters (std::nullopt = No fill)
    static std::optional<QColor> getFillColor(const QVariantMap& parameters)
    {
        if (parameters.value(name(PDFDocumentToolRedact::PARAMETER_NO_FILL)).toBool())
        {
            return std::nullopt;
        }
        QColor color = parameters.value(name(PDFDocumentToolRedact::PARAMETER_FILL_COLOR)).value<QColor>();
        if (!color.isValid())
        {
            color = QColor(0, 0, 0);
        }
        color.setAlpha(255);
        return color;
    }

    /// The text of the kept parts (437:13887 "Title and metadata"; review RS6FIGMA): whole phrases for
    /// the translators, "and" before the last part
    static QString getKeptText(bool isTitleKept, bool isMetadataKept, bool isOutlineKept)
    {
        QStringList parts;
        if (isTitleKept)
        {
            parts << tr("title");
        }
        if (isMetadataKept)
        {
            parts << tr("metadata");
        }
        if (isOutlineKept)
        {
            parts << tr("outline");
        }

        QString text;
        switch (parts.size())
        {
            case 0:
                return tr("Nothing");
            case 1:
                text = parts.front();
                break;
            case 2:
                text = tr("%1 and %2", "Kept parts of a redacted copy").arg(parts[0], parts[1]);
                break;
            default:
                text = tr("%1, %2 and %3", "Kept parts of a redacted copy").arg(parts[0], parts[1], parts[2]);
                break;
        }
        text[0] = text[0].toUpper();
        return text;
    }

    /// The match does not lie on one line (K6.21): its characters are not all on one (page, block,
    /// line). The items are ranges of the characters - a space guessed by the text flow is no character,
    /// so the words of one line may be more items.
    static bool isAcrossLines(const PDFDocumentTextSearchMatch& match)
    {
        if (match.textSelectionItems.empty())
        {
            return false;
        }

        const PDFCharacterPointer& reference = match.textSelectionItems.front().first;
        for (const PDFTextSelectionItem& item : match.textSelectionItems)
        {
            for (const PDFCharacterPointer& pointer : { item.first, item.second })
            {
                if (pointer.pageIndex != reference.pageIndex || pointer.blockIndex != reference.blockIndex || pointer.lineIndex != reference.lineIndex)
                {
                    return true;
                }
            }
        }
        return false;
    }
};

QString PDFDocumentToolRedact::createLineBreakPattern(const QString& query)
{
    static const QRegularExpression whiteSpace(QStringLiteral("\\s+"), QRegularExpression::UseUnicodePropertiesOption);

    // Each run of white space of the term is any run of white space of the text (the end of a line is a
    // space of the text flow, a line may end by spaces of its own); the rest is literal
    QString pattern;
    qsizetype position = 0;
    QRegularExpressionMatchIterator iterator = whiteSpace.globalMatch(query);
    while (iterator.hasNext())
    {
        const QRegularExpressionMatch match = iterator.next();
        pattern += QRegularExpression::escape(query.mid(position, match.capturedStart() - position));
        pattern += QStringLiteral("\\s+");
        position = match.capturedEnd();
    }
    pattern += QRegularExpression::escape(query.mid(position));
    return pattern;
}

PDFDocumentTextSearchResult PDFDocumentToolRedact::findMatches(const PDFDocumentTextIndex& index, const QString& query, const PDFDocumentTextSearchOptions& options,
                                                               bool isAcrossLineBreaks, const PDFOperationControl* control)
{
    // K6.21: across line breaks a plain term is the expression of its words (createLineBreakPattern),
    // an expression of the user stays as it is
    QString searchedQuery = query;
    PDFDocumentTextSearchOptions searchOptions = options;
    if (isAcrossLineBreaks && !options.regularExpression)
    {
        searchedQuery = createLineBreakPattern(query);
        searchOptions.regularExpression = true;
        searchOptions.wildcards = false;
    }

    PDFDocumentTextSearchResult result = PDFDocumentTextSearch::find(index, searchedQuery, searchOptions, size_t(MAXIMUM_SEARCH_RESULT_COUNT), control);
    if (!isAcrossLineBreaks)
    {
        auto it = std::remove_if(result.results.begin(), result.results.end(), &PDFDocumentToolRedactHelper::isAcrossLines);
        result.results.erase(it, result.results.end());
    }
    return result;
}

QPainterPath PDFDocumentToolRedact::getMatchArea(const PDFDocumentTextSearchMatch& match)
{
    // The quadrilaterals have 4 points each: the top left, the top right, the bottom left and the
    // bottom right corner (the order of /QuadPoints); the area of each is its polygon
    QPainterPath area;
    area.setFillRule(Qt::WindingFill);
    const QPolygonF& points = match.quadrilaterals;
    for (qsizetype i = 0; i + 3 < points.size(); i += 4)
    {
        QPolygonF polygon;
        polygon << points[i] << points[i + 1] << points[i + 3] << points[i + 2];
        area.addPolygon(polygon);
        area.closeSubpath();
    }
    return area;
}

QString PDFDocumentToolRedact::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolRedact::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolPageSelection noPage;
    noPage.isNone = true;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_USE_MARKS), QT_TR_NOOP("Use the marks"),
                                                 QT_TR_NOOP("The Redact annotations of the document are redacted"), true));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_AREAS), QT_TR_NOOP("Areas"),
                                                   QT_TR_NOOP("JSON array of the areas: [{\"page\": 1, \"rect\": [x, y, width, height]}] in points of the page, the origin at its lower left corner"),
                                                   QString()).setStructuredData(true).setMaximumLength(MAXIMUM_AREAS_LENGTH).setStored(false).setSensitive(true));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_SEARCH), QT_TR_NOOP("Search"),
                                                   QT_TR_NOOP("Every match of this text is redacted"), QString(), MAXIMUM_QUERY_LENGTH).setStored(false).setSensitive(true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SEARCH_REGEX), QT_TR_NOOP("Regular expression"),
                                                 QT_TR_NOOP("The search text is a regular expression"), false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SEARCH_CASE_SENSITIVE), QT_TR_NOOP("Match case"),
                                                 QT_TR_NOOP("Upper and lower case letters differ"), false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SEARCH_WHOLE_WORDS), QT_TR_NOOP("Whole words"),
                                                 QT_TR_NOOP("Only whole words match"), false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SEARCH_ACROSS_LINE_BREAKS), QT_TR_NOOP("Across line breaks"),
                                                 QT_TR_NOOP("The end of a line counts as a space"), false));
    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_PAGES), QT_TR_NOOP("Pages"),
                                                          QT_TR_NOOP("Whole pages, which are redacted"), noPage).setStored(false));
    definitions.push_back(Definition::createColor(tr, QString::fromLatin1(PARAMETER_FILL_COLOR), QT_TR_NOOP("Fill"), QT_TR_NOOP("Color of the redacted areas"), QColor(0, 0, 0)));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_NO_FILL), QT_TR_NOOP("No fill"),
                                                 QT_TR_NOOP("The redacted areas stay empty"), false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_KEEP_TITLE), QT_TR_NOOP("Keep the title"),
                                                 QT_TR_NOOP("The title of the document is kept"), true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_KEEP_METADATA), QT_TR_NOOP("Keep the metadata"),
                                                 QT_TR_NOOP("The author, the subject, the keywords and the dates are kept"), true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_KEEP_OUTLINE), QT_TR_NOOP("Keep the outline"),
                                                 QT_TR_NOOP("The outline (bookmarks) is kept"), false));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolRedact::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolRedactHelper;

    // A value, which nothing uses, would be ignored silently: the assistant would report a result
    // it did not get
    std::vector<PDFDocumentToolParameterError> errors;
    const bool hasSearch = !given.value(Helper::name(PARAMETER_SEARCH)).toString().isEmpty();
    for (const char* parameter : { PARAMETER_SEARCH_REGEX, PARAMETER_SEARCH_CASE_SENSITIVE, PARAMETER_SEARCH_WHOLE_WORDS, PARAMETER_SEARCH_ACROSS_LINE_BREAKS })
    {
        if (!hasSearch && given.contains(Helper::name(parameter)))
        {
            errors.push_back({ Helper::name(parameter), tr("%1 is used only with search: set search, or leave %1 out.").arg(Helper::name(parameter)) });
        }
    }
    if (given.value(Helper::name(PARAMETER_NO_FILL)).toBool() && given.contains(Helper::name(PARAMETER_FILL_COLOR)))
    {
        errors.push_back({ Helper::name(PARAMETER_FILL_COLOR), tr("fillColor is not used with noFill: leave fillColor out, or set noFill to false.") });
    }
    return errors;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolRedact::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolRedactHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty() || context.documents.empty())
    {
        return errors;
    }

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();

    // R6-9, B139: the copy is a new document without encryption - the rule of Merge (a restricted
    // document needs its owner), checked in the core for every caller (PdfTool, MCP)
    const PDFDocumentMerger::Permission permission = PDFDocumentMerger::getPermission(document, input.isOwnerPasswordVerified);
    if (permission != PDFDocumentMerger::Permission::Permitted)
    {
        errors.push_back({ QString(), PDFDocumentMerger::getPermissionText(permission, input.fileName.isEmpty() ? tr("The document") : input.fileName) });
        return errors;
    }

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (pageCount == 0)
    {
        errors.push_back({ QString(), tr("The document has no pages.") });
        return errors;
    }

    bool hasArea = false;

    // The marks (plan 7.2: the limits of the marks)
    if (parameters.value(Helper::name(PARAMETER_USE_MARKS)).toBool())
    {
        QString markError;
        const std::vector<PDFRedact::Area> marks = PDFRedact::getMarkAreas(document, &markError);
        if (!markError.isEmpty())
        {
            errors.push_back({ Helper::name(PARAMETER_USE_MARKS), markError });
        }
        hasArea = hasArea || !marks.empty();
    }

    // The areas of MCP
    QString areasError;
    const std::optional<std::vector<Helper::AreaEntry>> areas = Helper::parseAreas(parameters.value(Helper::name(PARAMETER_AREAS)).toString(), &areasError);
    if (!areas)
    {
        errors.push_back({ Helper::name(PARAMETER_AREAS), areasError });
    }
    else
    {
        for (const Helper::AreaEntry& area : *areas)
        {
            if (area.pageIndex >= pageCount)
            {
                errors.push_back({ Helper::name(PARAMETER_AREAS), tr("Page %1 does not exist, the document has %Ln pages.", nullptr, int(qMin<PDFInteger>(pageCount, std::numeric_limits<int>::max()))).arg(area.pageIndex + 1) });
                break;
            }
        }
        hasArea = hasArea || !areas->empty();
    }

    // The search: a regular expression must be valid (the length is limited by the definition)
    const QString search = parameters.value(Helper::name(PARAMETER_SEARCH)).toString();
    if (!search.isEmpty())
    {
        const PDFDocumentTextSearchOptions options = Helper::getSearchOptions(parameters);
        const QString pattern = PDFDocumentTextSearch::createPattern(search, options);
        if (!pattern.isEmpty() && !QRegularExpression(pattern).isValid())
        {
            errors.push_back({ Helper::name(PARAMETER_SEARCH), tr("The regular expression is not valid.") });
        }
        hasArea = true;
    }

    // The whole pages
    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters.value(Helper::name(PARAMETER_PAGES)), nullptr);
    if (selection && !selection->isNone)
    {
        QString pagesError;
        const std::vector<PDFInteger> pages = selection->getPageIndices(document, &pagesError);
        if (!pagesError.isEmpty())
        {
            errors.push_back({ Helper::name(PARAMETER_PAGES), pagesError });
        }
        hasArea = hasArea || !pages.empty();
    }

    if (errors.empty() && !hasArea)
    {
        errors.push_back({ QString(), tr("Nothing is marked for redaction.") });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolRedact::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolRedactHelper;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();

    const QString search = parameters.value(Helper::name(PARAMETER_SEARCH)).toString();
    const std::optional<QColor> fillColor = Helper::getFillColor(parameters);
    const bool isTitleKept = parameters.value(Helper::name(PARAMETER_KEEP_TITLE)).toBool();
    const bool isMetadataKept = parameters.value(Helper::name(PARAMETER_KEEP_METADATA)).toBool();
    const bool isOutlineKept = parameters.value(Helper::name(PARAMETER_KEEP_OUTLINE)).toBool();

    PDFProgressPhases phases(progress);
    const int searchPhase = !search.isEmpty() ? phases.addPhase(15, tr("Finding the text"), true) : -1;
    const int redactPhase = phases.addPhase(45, tr("Redacting pages"), true);
    const int writePhase = phases.addPhase(5, tr("Writing the copy"), false);
    const int checkPhase = phases.addPhase(35, tr("Checking the copy"), true);
    phases.start();

    // The layers (R6-15): the states of the tab in the GUI, OCUsage::Export for MCP and PdfTool; a
    // layer OFF is not carried over
    const std::optional<std::map<PDFObjectReference, OCState>>& layerStates = context.rendering.layerStates;
    PDFOptionalContentActivity optionalContentActivity(document, layerStates ? OCUsage::View : OCUsage::Export, nullptr);
    if (layerStates)
    {
        for (const auto& [ocg, state] : *layerStates)
        {
            optionalContentActivity.setState(ocg, state, false);
        }
    }
    const std::map<PDFObjectReference, OCState> states = optionalContentActivity.getStates();
    const bool hasHiddenLayers = std::any_of(states.cbegin(), states.cend(), [](const auto& item) { return item.second == OCState::OFF; });

    // The areas: the marks, the areas of MCP, the matches of the search, the whole pages
    PDFRedact::Settings settings;
    settings.fillColor = fillColor;
    settings.options.setFlag(PDFRedact::CopyTitle, isTitleKept);
    settings.options.setFlag(PDFRedact::CopyMetadata, isMetadataKept);
    settings.options.setFlag(PDFRedact::CopyOutline, isOutlineKept);

    qint64 areaCount = 0;
    if (parameters.value(Helper::name(PARAMETER_USE_MARKS)).toBool())
    {
        QString markError;
        std::vector<PDFRedact::Area> marks = PDFRedact::getMarkAreas(document, &markError);
        if (!markError.isEmpty())
        {
            return PDFDocumentToolResult::createFailed(markError);
        }
        areaCount += qint64(marks.size());
        settings.areas.insert(settings.areas.end(), std::make_move_iterator(marks.begin()), std::make_move_iterator(marks.end()));
    }

    QString errorMessage;
    const std::optional<std::vector<Helper::AreaEntry>> areas = Helper::parseAreas(parameters.value(Helper::name(PARAMETER_AREAS)).toString(), &errorMessage);
    if (!areas)
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }
    for (const Helper::AreaEntry& area : *areas)
    {
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        path.addRect(area.rect);
        settings.areas.push_back({ area.pageIndex, path });
        ++areaCount;
    }

    if (!search.isEmpty())
    {
        phases.beginPhase(searchPhase);
        const std::shared_ptr<const PDFDocumentTextIndex> index = PDFDocumentTextIndex::build(document, &optionalContentActivity, control, phases.getPhase(searchPhase));
        if (PDFOperationControl::isOperationCanceled(control) || !index)
        {
            return PDFDocumentToolResult::createStopped();
        }

        const bool isAcrossLineBreaks = parameters.value(Helper::name(PARAMETER_SEARCH_ACROSS_LINE_BREAKS)).toBool();
        const PDFDocumentTextSearchResult found = findMatches(*index, search, Helper::getSearchOptions(parameters), isAcrossLineBreaks, control);
        if (found.isCanceled)
        {
            return PDFDocumentToolResult::createStopped();
        }
        if (!found.error.isEmpty())
        {
            return PDFDocumentToolResult::createFailed(found.error);
        }
        if (found.isTruncated)
        {
            // Plan 7.2: never a part of the matches
            return PDFDocumentToolResult::createFailed(tr("The search has more than %1 matches. Search for a longer text.").arg(MAXIMUM_SEARCH_RESULT_COUNT));
        }

        for (const PDFDocumentTextSearchMatch& match : found.results)
        {
            settings.areas.push_back({ match.pageIndex, getMatchArea(match) });
            ++areaCount;
        }
    }

    settings.wholePages = Helper::getWholePages(document, parameters, &errorMessage);
    areaCount += qint64(settings.wholePages.size());

    if (settings.areas.empty() && settings.wholePages.empty())
    {
        return PDFDocumentToolResult::createNothingToDo(search.isEmpty() ? tr("Nothing is marked for redaction.") : tr("The search found nothing to redact."));
    }

    // The rebuild of the pages (owner answer Q6.8 a)
    phases.beginPhase(redactPhase);
    PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(document);
    cmsManager.setSettings(context.rendering.cmsSettings ? *context.rendering.cmsSettings : cmsManager.getDefaultSettings());
    const PDFCMSPointer cms = cmsManager.getCurrentCMS();
    const PDFMeshQualitySettings meshQualitySettings;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    const PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    PDFRedact redact(document, &fontCache, &iccProfileCache, cms.get(), &optionalContentActivity, &meshQualitySettings, QColor());
    PDFRedact::Result redacted = redact.perform(settings, control, phases.getPhase(redactPhase));
    if (redacted.isStopped || PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    // The copy written into memory (one revision, no encryption)
    phases.beginPhase(writePhase);
    QByteArray writtenData = PDFDocumentToolNewDocument::write(&redacted.document, &errorMessage);
    if (writtenData.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    // The check of the written bytes (6.8); a finding means no copy (owner answer Q9)
    phases.beginPhase(checkPhase);
    std::vector<PDFRedactionVerifier::Area> checkedAreas;
    checkedAreas.reserve(redacted.areas.size());
    for (const PDFRedact::Area& area : redacted.areas)
    {
        checkedAreas.push_back({ area.pageIndex, area.path });
    }
    PDFRedactionVerifier::Options checkOptions;
    checkOptions.fillColor = fillColor;
    checkOptions.isTitleKept = isTitleKept;
    checkOptions.isMetadataKept = isMetadataKept;
    checkOptions.isOutlineKept = isOutlineKept;
    const PDFRedactionVerifier::Result check = PDFRedactionVerifier::verify(document, checkedAreas, writtenData, checkOptions, control, phases.getPhase(checkPhase));
    if (check.isStopped || PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }
    if (!check.errors.empty())
    {
        QStringList messages;
        for (const PDFRedactionVerifier::Finding& finding : check.errors)
        {
            if (!messages.contains(finding.message))
            {
                messages << finding.message;
            }
        }
        constexpr qsizetype MAXIMUM_MESSAGES = 5;
        if (messages.size() > MAXIMUM_MESSAGES)
        {
            messages.resize(MAXIMUM_MESSAGES);
            messages << QStringLiteral("…");
        }
        return PDFDocumentToolResult::createFailed(tr("The redacted copy was not created. %1").arg(messages.join(QLatin1Char(' '))));
    }
    phases.finish();

    // The result (437:13887): never a redacted text, a search term or an area
    PDFDocumentToolResult result;
    std::set<PDFInteger> redactedPages;
    for (const PDFRedact::Area& area : redacted.areas)
    {
        redactedPages.insert(area.pageIndex);
    }

    const QString areasText = tr("%Ln areas", nullptr, int(qMin<qint64>(areaCount, std::numeric_limits<int>::max())));
    const QString pagesText = tr("%Ln pages", nullptr, int(redactedPages.size()));
    result.facts.push_back({ QString::fromLatin1(FACT_REMOVED), tr("Removed"), tr("%1 on %2", "areas on pages").arg(areasText, pagesText) });
    result.facts.push_back({ QString::fromLatin1(FACT_FILL), tr("Fill"), Helper::getFillName(fillColor) });
    result.facts.push_back({ QString::fromLatin1(FACT_KEPT), tr("Kept"), Helper::getKeptText(isTitleKept, isMetadataKept, isOutlineKept) });
    if (PDFForm::getSignatureCount(document) > 0)
    {
        result.facts.push_back({ QString::fromLatin1(FACT_SIGNATURES), tr("Signatures"), tr("Not carried over") });
    }
    const QStringList conformance = getConformanceDeclarations(document);
    if (!conformance.isEmpty())
    {
        PDFDocumentToolFact fact = createConformanceFact(conformance);
        fact.id = QString::fromLatin1(FACT_CONFORMANCE);
        result.facts.push_back(std::move(fact));
    }
    if (hasHiddenLayers)
    {
        result.facts.push_back({ QString::fromLatin1(FACT_LAYERS), tr("Layers"), tr("Hidden layers were not carried over") });
    }
    if (!redacted.pageProblems.empty())
    {
        const QString value = redacted.pageProblems.size() == 1 ? redacted.pageProblems.front().message : tr("%Ln pages", nullptr, int(redacted.pageProblems.size()));
        result.facts.push_back({ QString::fromLatin1(FACT_PAGE_PROBLEMS), tr("Rendering issues"), value });
        for (const PDFRedact::PageProblem& problem : redacted.pageProblems)
        {
            result.renderingIssues.push_back({ problem.pageIndex, problem.message });
        }
    }

    // The warnings (owner answers Q6.10, Q6.19): the rows of the steps and the data of MCP
    QJsonArray warnings;
    for (const PDFRedactionVerifier::Finding& warning : check.warnings)
    {
        PDFDocumentToolStep step;
        step.text = warning.message;
        step.state = PDFDocumentToolStep::State::Warning;
        result.steps.push_back(std::move(step));
        warnings.append(warning.message);
    }

    result.data.insert(QString::fromLatin1(DATA_AREA_COUNT), areaCount);
    result.data.insert(QString::fromLatin1(DATA_PAGE_COUNT), qint64(redactedPages.size()));
    result.data.insert(QString::fromLatin1(DATA_VERIFIED), true);
    result.data.insert(QString::fromLatin1(DATA_WARNINGS), warnings);

    const QString baseName = QFileInfo(input.fileName).completeBaseName();
    PDFDocumentToolNewDocument::addToResult(&result, std::move(redacted.document), std::move(writtenData),
                                            (baseName.isEmpty() ? QStringLiteral("document") : baseName) + QStringLiteral("_REDACTED.pdf"));
    return result;
}

QVariantMap PDFDocumentToolRedact::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    using Helper = PDFDocumentToolRedactHelper;

    if (!from || !to || from == to)
    {
        return parameters;
    }

    // The whole pages and the areas follow their page objects; an area of a page, which is not in
    // the other version, is dropped
    QVariantMap mapped = parameters;
    const std::vector<PDFInteger> pageMap = PDFDocumentToolPageSelection::mapPageIndices(from, to);

    const QString pagesName = Helper::name(PARAMETER_PAGES);
    if (mapped.contains(pagesName))
    {
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(mapped.value(pagesName), nullptr);
        if (selection)
        {
            mapped.insert(pagesName, selection->mapPages(from, pageMap).toVariant());
        }
    }

    const QString areasName = Helper::name(PARAMETER_AREAS);
    QString error;
    const std::optional<std::vector<Helper::AreaEntry>> areas = Helper::parseAreas(mapped.value(areasName).toString(), &error);
    if (areas && !areas->empty())
    {
        std::vector<Helper::AreaEntry> mappedAreas;
        for (Helper::AreaEntry area : *areas)
        {
            if (area.pageIndex >= 0 && size_t(area.pageIndex) < pageMap.size() && pageMap[size_t(area.pageIndex)] >= 0)
            {
                area.pageIndex = pageMap[size_t(area.pageIndex)];
                mappedAreas.push_back(area);
            }
        }
        mapped.insert(areasName, Helper::createAreasText(mappedAreas));
    }
    return mapped;
}

}   // namespace pdf
