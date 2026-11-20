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

#include "pdfdocumenttool_measure.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfoperationcontrol.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include "pdfdbgheap.h"

namespace pdf
{

/// The parts of the parameters of Measure
class PDFDocumentToolMeasureHelper
{
public:
    PDFDocumentToolMeasureHelper() = delete;

    /// The mode of the normalized parameters (list without one)
    static QString getMode(const QVariantMap& parameters)
    {
        const QString mode = parameters.value(QLatin1String(PDFDocumentToolMeasure::PARAMETER_MODE)).toString();
        return mode.isEmpty() ? QString::fromLatin1(PDFDocumentToolMeasure::MODE_LIST) : mode;
    }

    /// The options of a table of PDFMeasureUnits (the titles are its symbols, the descriptions its names -
    /// literals, PDFDocumentToolText: the table is a template argument, so its texts are constant expressions)
    template<const auto& Definitions, size_t... Indices>
    static std::vector<PDFDocumentToolParameterOption> createUnitOptions(std::index_sequence<Indices...>)
    {
        return { PDFDocumentToolParameterOption(QString::fromLatin1(Definitions[Indices].id), PDFDocumentToolText(Definitions[Indices].symbol), PDFDocumentToolText(Definitions[Indices].name))... };
    }

    /// The parameter of the unit of a quantity: an enumeration of the ids of a table of PDFMeasureUnits,
    /// translated in the context of the units (their symbols are the titles of the options), the default the
    /// unit of the frames (PDFMeasureUnits::INITIAL_*_UNIT)
    template<const auto& Definitions>
    static PDFDocumentToolParameterDefinition createUnitParameter(const char* name, PDFDocumentToolText title, PDFDocumentToolText description, const char* defaultId)
    {
        std::vector<PDFDocumentToolParameterOption> options = createUnitOptions<Definitions>(std::make_index_sequence<std::size(Definitions)>());
        return PDFDocumentToolParameterDefinition::createEnum(PDFMeasureUnits::tr, QString::fromLatin1(name), title, description, std::move(options), QString::fromLatin1(defaultId));
    }

    /// A number of a JSON value (a double or an integer), std::nullopt for another type or a value that is not finite
    static std::optional<PDFReal> getNumber(const QJsonValue& value)
    {
        if (!value.isDouble())
        {
            return std::nullopt;
        }
        const PDFReal number = value.toDouble();
        return std::isfinite(number) ? std::optional<PDFReal>(number) : std::nullopt;
    }

    /// The scale of a JSON object { "paperValue", "paperUnit", "realValue", "realUnit", "name" }
    static std::optional<PDFMeasureScale> getScale(const QJsonValue& value)
    {
        if (!value.isObject())
        {
            return std::nullopt;
        }
        const QJsonObject object = value.toObject();
        const std::optional<PDFReal> paperValue = getNumber(object.value(QStringLiteral("paperValue")));
        const std::optional<PDFReal> realValue = getNumber(object.value(QStringLiteral("realValue")));
        const QJsonValue name = object.value(QStringLiteral("name"));
        if (!paperValue || !realValue || !object.value(QStringLiteral("paperUnit")).isString() || !object.value(QStringLiteral("realUnit")).isString() ||
            !(name.isUndefined() || name.isString()) || name.toString().size() > PDFMeasureScale::MAXIMUM_DOCUMENT_NAME_LENGTH)
        {
            return std::nullopt;
        }

        PDFMeasureScale scale(*paperValue, object.value(QStringLiteral("paperUnit")).toString().toLatin1(), *realValue, object.value(QStringLiteral("realUnit")).toString().toLatin1(), name.toString());
        return scale.isValid() ? std::optional<PDFMeasureScale>(scale) : std::nullopt;
    }

    /// The effective /UserUnit of a page (B209), 1 for a page that does not exist
    static PDFReal getUserUnit(const PDFDocument* document, PDFInteger pageIndex)
    {
        const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
        if (!catalog || pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
        {
            return 1.0;
        }
        return catalog->getPage(size_t(pageIndex))->getEffectiveUserUnit();
    }

    /// The unit of the quantity of a kind
    static PDFMeasureUnit getUnit(PDFMeasureKind kind, const PDFMeasureDisplayUnits& units)
    {
        const PDFMeasureQuantity quantity = PDFMeasureGeometry::getQuantity(kind);
        switch (quantity)
        {
            case PDFMeasureQuantity::Length:
                return PDFMeasureUnits::findUnitOrDefault(quantity, units.length);
            case PDFMeasureQuantity::Area:
                return PDFMeasureUnits::findUnitOrDefault(quantity, units.area);
            case PDFMeasureQuantity::Angle:
                return PDFMeasureUnits::findUnitOrDefault(quantity, units.angle);
        }
        return PDFMeasureUnit();
    }

    /// A color with the opacity in percent (the alpha of the label)
    static QColor getColor(const QVariantMap& parameters, const char* colorName, const char* opacityName, const QColor& defaultColor, int defaultOpacity)
    {
        QColor color = parameters.value(QString::fromLatin1(colorName)).value<QColor>();
        if (!color.isValid())
        {
            color = defaultColor;
        }
        bool isOk = false;
        int opacity = parameters.value(QString::fromLatin1(opacityName)).toInt(&isOk);
        if (!isOk)
        {
            opacity = defaultOpacity;
        }
        color.setAlphaF(qBound(0, opacity, 100) / 100.0);
        return color;
    }

    /// The row of the data of a measurement
    static QJsonObject createRow(PDFInteger pageIndex, PDFMeasureKind kind, PDFReal value, const QString& unit, const QString& unitLabel, const QString& text, const QString& scale, bool isNew)
    {
        QJsonObject row;
        row.insert(QStringLiteral("page"), qint64(pageIndex + 1));
        row.insert(QStringLiteral("kind"), PDFDocumentToolMeasure::getKindId(kind));
        row.insert(QStringLiteral("value"), value);
        row.insert(QStringLiteral("unit"), unit);
        row.insert(QStringLiteral("unitLabel"), unitLabel);
        row.insert(QStringLiteral("text"), text);
        row.insert(QStringLiteral("scale"), scale);
        row.insert(QStringLiteral("source"), isNew ? QStringLiteral("new") : QStringLiteral("annotation"));
        return row;
    }
};

QString PDFDocumentToolMeasure::getKindId(PDFMeasureKind kind)
{
    switch (kind)
    {
        case PDFMeasureKind::Distance:
            return QString::fromLatin1(KIND_DISTANCE);
        case PDFMeasureKind::Perimeter:
            return QString::fromLatin1(KIND_PERIMETER);
        case PDFMeasureKind::Area:
            return QString::fromLatin1(KIND_AREA);
        case PDFMeasureKind::Angle:
            return QString::fromLatin1(KIND_ANGLE);
    }
    return QString();
}

std::optional<PDFMeasureKind> PDFDocumentToolMeasure::getKind(const QString& id)
{
    for (const PDFMeasureKind kind : { PDFMeasureKind::Distance, PDFMeasureKind::Perimeter, PDFMeasureKind::Area, PDFMeasureKind::Angle })
    {
        if (getKindId(kind) == id)
        {
            return kind;
        }
    }
    return std::nullopt;
}

QString PDFDocumentToolMeasure::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolMeasure::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolMeasureHelper;

    PDFDocumentToolParameterDefinitions definitions;
    std::vector<PDFDocumentToolParameterOption> modes;
    modes.push_back({ QString::fromLatin1(MODE_LIST), QT_TR_NOOP("List"), QT_TR_NOOP("The measurements and the viewports of the pages as data") });
    modes.push_back({ QString::fromLatin1(MODE_CSV), QT_TR_NOOP("CSV"), QT_TR_NOOP("The measurements as a CSV file") });
    modes.push_back({ QString::fromLatin1(MODE_ADD), QT_TR_NOOP("Add as annotations"), QT_TR_NOOP("The measurements become measurement annotations of the document") });
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_MODE), QT_TR_NOOP("Mode"), QT_TR_NOOP("What is done with the measurements"), std::move(modes), QString::fromLatin1(MODE_LIST)));

    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_PAGES), QT_TR_NOOP("Pages"), QT_TR_NOOP("Pages, whose measurement annotations and viewports are listed")));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_MEASUREMENTS), QT_TR_NOOP("Measurements"),
                                                   QT_TR_NOOP("JSON array of the measurements: [{\"page\": 1, \"kind\": \"distance\", \"points\": [[x, y], [x, y]], \"scale\": {\"paperValue\": 1, \"paperUnit\": \"mm\", \"realValue\": 50, \"realUnit\": \"mm\"}}] in points of the page, the origin at its lower left corner"),
                                                   QString()).setStructuredData(true).setMaximumLength(MAXIMUM_MEASUREMENTS_LENGTH).setStored(false));

    definitions.push_back(Helper::createUnitParameter<PDFMeasureUnits::LENGTH_UNITS>(PARAMETER_LENGTH_UNIT, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Lengths"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Unit of the distances and perimeters"), PDFMeasureUnits::INITIAL_LENGTH_UNIT));
    definitions.push_back(Helper::createUnitParameter<PDFMeasureUnits::AREA_UNITS>(PARAMETER_AREA_UNIT, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Areas"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Unit of the areas"), PDFMeasureUnits::INITIAL_AREA_UNIT));
    definitions.push_back(Helper::createUnitParameter<PDFMeasureUnits::ANGLE_UNITS>(PARAMETER_ANGLE_UNIT, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Angles"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Unit of the angles"), PDFMeasureUnits::INITIAL_ANGLE_UNIT));

    // The panel Labels (471:5490): the look of the labels on the canvas and of the annotations of add
    const PDFMeasureAnnotationStyle style;
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_FONT_FAMILY), QT_TR_NOOP("Font"), QT_TR_NOOP("Font of the labels on the canvas; the annotations use the standard annotation font"), style.fontFamily, 256));
    definitions.push_back(Definition::createReal(tr, QString::fromLatin1(PARAMETER_FONT_SIZE), QT_TR_NOOP("Font size"), QT_TR_NOOP("Size of the font of the labels in points"), style.fontSize, 4.0, 144.0));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_BOLD), QT_TR_NOOP("Bold"), QT_TR_NOOP("The labels are bold"), style.isBold));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_ITALIC), QT_TR_NOOP("Italic"), QT_TR_NOOP("The labels are italic"), style.isItalic));
    definitions.push_back(Definition::createColor(tr, QString::fromLatin1(PARAMETER_TEXT_COLOR), QT_TR_NOOP("Text color"), QT_TR_NOOP("Color of the text of the labels and of the lines"), QColor(Qt::black)));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_TEXT_OPACITY), QT_TR_NOOP("Text opacity"), QT_TR_NOOP("Opacity of the text of the labels in percent"), DEFAULT_TEXT_OPACITY, 0, 100));
    definitions.push_back(Definition::createColor(tr, QString::fromLatin1(PARAMETER_BACKGROUND_COLOR), QT_TR_NOOP("Background color"), QT_TR_NOOP("Color of the background of the labels and of the areas"), QColor(Qt::black)));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_BACKGROUND_OPACITY), QT_TR_NOOP("Background opacity"), QT_TR_NOOP("Opacity of the background of the labels in percent"), DEFAULT_BACKGROUND_OPACITY, 0, 100));

    // The state of the panel (the last values of the tool, never MCP)
    std::vector<PDFDocumentToolParameterOption> kinds;
    kinds.push_back({ QString::fromLatin1(DRAW_HORIZONTAL), QT_TR_NOOP("Horizontal"), QT_TR_NOOP("Two points, measured horizontally") });
    kinds.push_back({ QString::fromLatin1(DRAW_VERTICAL), QT_TR_NOOP("Vertical"), QT_TR_NOOP("Two points, measured vertically") });
    kinds.push_back({ QString::fromLatin1(DRAW_LINEAR), QT_TR_NOOP("Linear"), QT_TR_NOOP("Distance between two points") });
    kinds.push_back({ QString::fromLatin1(DRAW_PERIMETER), QT_TR_NOOP("Perimeter"), QT_TR_NOOP("Click the corners, finish on the first point") });
    kinds.push_back({ QString::fromLatin1(DRAW_RECTANGLE_PERIMETER), QT_TR_NOOP("Rectangle Perimeter"), QT_TR_NOOP("Two opposite corners") });
    kinds.push_back({ QString::fromLatin1(DRAW_AREA), QT_TR_NOOP("Area"), QT_TR_NOOP("Click the corners, finish on the first point") });
    kinds.push_back({ QString::fromLatin1(DRAW_RECTANGLE_AREA), QT_TR_NOOP("Rectangle Area"), QT_TR_NOOP("Two opposite corners") });
    kinds.push_back({ QString::fromLatin1(DRAW_ANGLE), QT_TR_NOOP("Angle"), QT_TR_NOOP("Three points, the second one is the vertex") });
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_KIND), QT_TR_NOOP("Kind of measurement"), QT_TR_NOOP("What a drawing on the page measures"), std::move(kinds), QString::fromLatin1(DRAW_LINEAR)).setPanelOnly(true));

    std::vector<PDFDocumentToolParameterOption> storages;
    storages.push_back({ QString::fromLatin1(STORAGE_TEMPORARY), QT_TR_NOOP("Temporary measurements"), QT_TR_NOOP("Drawn over the document and lost when it is closed. They always use the current scale; the document is not changed."), QStringLiteral("pin-off") });
    storages.push_back({ QString::fromLatin1(STORAGE_ANNOTATIONS), QT_TR_NOOP("Annotations in the document"), QT_TR_NOOP("Every measurement is added to the document as a measurement annotation. The scale used is stored in it."), QStringLiteral("message-square") });
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_STORAGE), QT_TR_NOOP("Storage"), QT_TR_NOOP("Where new measurements are kept"), std::move(storages), QString::fromLatin1(STORAGE_TEMPORARY)).setPanelOnly(true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_REMEMBER_SCALE), QT_TR_NOOP("Remember the scale per document"), QT_TR_NOOP("The scale of a document is remembered for it"), true).setPanelOnly(true));
    return definitions;
}

PDFDocumentToolResultKind PDFDocumentToolMeasure::getResultKind(const QVariantMap& parameters) const
{
    // Plan 4.8: add changes the document (the other kind of the row of the catalog), the others are analyses
    return PDFDocumentToolMeasureHelper::getMode(parameters) == QLatin1String(MODE_ADD) ? PDFDocumentToolResultKind::ChangeDocument : PDFDocumentToolResultKind::Analysis;
}

PDFDocumentToolPreselectedResult PDFDocumentToolMeasure::getPreselectedResult(const QVariantMap& parameters) const
{
    return PDFDocumentToolExtendedData::getDefaultPreselectedResult(getResultKind(parameters));
}

PDFDocumentToolSignatureImpact PDFDocumentToolMeasure::getSignatureImpact(const QVariantMap& parameters) const
{
    // New annotations are an incremental update of a signed file (its signatures stay, rules of step 1)
    return PDFDocumentToolExtendedData::getDefaultSignatureImpact(getResultKind(parameters));
}

std::vector<PDFSecurityHandler::Permission> PDFDocumentToolMeasure::getRequiredPermissions(const QVariantMap& parameters) const
{
    // Q7.2: the annotations of add need bit 6 ("add or modify text annotations, fill in interactive form
    // fields", Table 22); listing and exporting the measurements is reading the document
    std::vector<PDFSecurityHandler::Permission> permissions = PDFDocumentTool::getRequiredPermissions(parameters);
    if (getResultKind(parameters) == PDFDocumentToolResultKind::ChangeDocument)
    {
        permissions.push_back(PDFSecurityHandler::Permission::ModifyInteractiveItems);
    }
    return permissions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolMeasure::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    const PDFDocument* document = context.getDocument();
    if (!document)
    {
        return errors;
    }

    QString errorMessage;
    const std::optional<std::vector<PDFMeasureItem>> items = parseMeasurements(parameters.value(QString::fromLatin1(PARAMETER_MEASUREMENTS)).toString(), document, &errorMessage);
    if (!items)
    {
        errors.push_back({ QString::fromLatin1(PARAMETER_MEASUREMENTS), errorMessage });
        return errors;
    }

    if (getResultKind(parameters) == PDFDocumentToolResultKind::ChangeDocument)
    {
        if (items->empty())
        {
            errors.push_back({ QString::fromLatin1(PARAMETER_MEASUREMENTS), tr("There is no measurement to add.") });
        }

        // 12.8.2.2, Table 257: a certification of P = 1 or 2 allows no new annotation
        if (!isAnnotationAllowedByCertification(document))
        {
            errors.push_back({ QString::fromLatin1(PARAMETER_MODE), tr("The certification of the document does not allow new annotations.") });
        }
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolMeasure::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(progress);

    const PDFDocument* document = context.getDocument();
    if (!document)
    {
        return PDFDocumentToolResult::createFailed(tr("There is no document."));
    }

    QString errorMessage;
    const std::optional<std::vector<PDFMeasureItem>> items = parseMeasurements(parameters.value(QString::fromLatin1(PARAMETER_MEASUREMENTS)).toString(), document, &errorMessage);
    if (!items)
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    if (PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    const PDFMeasureDisplayUnits units = getUnits(parameters);
    const QString mode = PDFDocumentToolMeasureHelper::getMode(parameters);
    const PDFDocumentToolInputDocument& input = context.documents.front();
    const QString baseName = QFileInfo(input.fileName).completeBaseName();

    if (mode == QLatin1String(MODE_ADD))
    {
        if (items->empty())
        {
            return PDFDocumentToolResult::createNothingToDo(tr("There is no measurement to add."));
        }
        if (!isAnnotationAllowedByCertification(document))
        {
            return PDFDocumentToolResult::createFailed(tr("The certification of the document does not allow new annotations."));
        }

        int added = 0;
        QJsonArray failed;
        std::optional<PDFDocument> changedDocument = addAnnotations(document, *items, units, getStyle(parameters, context.authorName), &added, &failed, &errorMessage);
        if (!changedDocument)
        {
            return PDFDocumentToolResult::createFailed(errorMessage);
        }

        PDFDocumentToolResult result;
        PDFDocumentToolOutputDocument output;
        output.kind = PDFDocumentToolOutputDocument::Kind::Changed;
        output.inputIndex = 0;
        output.document = PDFDocumentPointer(new PDFDocument(std::move(*changedDocument)));
        output.flags = PDFModifiedDocument::ModificationFlags(PDFModifiedDocument::Annotation);
        output.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + QStringLiteral(".pdf");
        result.documents.push_back(std::move(output));
        result.data.insert(QString::fromLatin1(DATA_ADDED), added);
        result.data.insert(QString::fromLatin1(DATA_FAILED), failed);
        result.facts.push_back({ QStringLiteral("added"), tr("Annotations"), tr("%Ln added", nullptr, added) });
        if (!failed.isEmpty())
        {
            const int failedCount = int(failed.size());
            result.facts.push_back({ QStringLiteral("failed"), tr("Not stored"), failedCount == 1 ? tr("%Ln measurement", nullptr, failedCount) : tr("%Ln measurements", nullptr, failedCount) });
        }
        return result;
    }

    // list and csv: the existing measurement annotations and the viewports of the pages
    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters.value(QString::fromLatin1(PARAMETER_PAGES)), &errorMessage);
    if (!selection)
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }
    const std::vector<PDFInteger> pageIndices = selection->getPageIndices(document, &errorMessage);
    if (pageIndices.empty())
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    PDFDocumentToolResult result;
    result.data = createData(document, pageIndices, *items, units, control);
    if (PDFOperationControl::isOperationCanceled(control))
    {
        // A canceled reading is never a list of fewer measurements
        return PDFDocumentToolResult::createStopped();
    }
    const int measurementCount = int(result.data.value(QString::fromLatin1(DATA_MEASUREMENTS)).toArray().size());
    result.facts.push_back({ QStringLiteral("measurements"), tr("Measurements"), measurementCount == 1 ? tr("%Ln measurement", nullptr, measurementCount) : tr("%Ln measurements", nullptr, measurementCount) });

    if (mode == QLatin1String(MODE_CSV))
    {
        PDFDocumentToolOutputFile output = writeOutput(result.data, parameters, PDFDocumentToolTableWriter::Options(), (baseName.isEmpty() ? tr("Document") : baseName) + tr(" - measurements"), &errorMessage);
        if (output.suggestedFileName.isEmpty())
        {
            return PDFDocumentToolResult::createFailed(errorMessage);
        }
        result.outputs.push_back(std::move(output));
    }
    return result;
}

std::optional<std::vector<PDFMeasureItem>> PDFDocumentToolMeasure::parseMeasurements(const QString& text, const PDFDocument* document, QString* errorMessage)
{
    using Helper = PDFDocumentToolMeasureHelper;

    auto fail = [errorMessage](const QString& message) -> std::optional<std::vector<PDFMeasureItem>>
    {
        if (errorMessage)
        {
            *errorMessage = message;
        }
        return std::nullopt;
    };

    std::vector<PDFMeasureItem> items;
    if (text.trimmed().isEmpty())
    {
        return items;
    }
    if (text.size() > MAXIMUM_MEASUREMENTS_LENGTH)
    {
        return fail(tr("The measurements are too long."));
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isArray())
    {
        return fail(tr("The measurements are not a JSON array."));
    }

    const QJsonArray array = json.array();
    if (array.size() > MAXIMUM_MEASUREMENT_COUNT)
    {
        return fail(tr("At most %1 measurements can be given at once.").arg(MAXIMUM_MEASUREMENT_COUNT));
    }

    const qint64 pageCount = document ? qint64(document->getCatalog()->getPageCount()) : 0;
    items.reserve(size_t(array.size()));
    for (qsizetype i = 0; i < array.size(); ++i)
    {
        const QString prefix = tr("Measurement %1: ").arg(i + 1);
        if (!array.at(i).isObject())
        {
            return fail(prefix + tr("it is not a JSON object."));
        }
        const QJsonObject object = array.at(i).toObject();

        PDFMeasureItem item;
        const std::optional<PDFReal> page = Helper::getNumber(object.value(QStringLiteral("page")));
        if (!page || *page != std::floor(*page) || *page < 1.0 || *page > PDFReal(pageCount))
        {
            return fail(prefix + tr("the page is not a page of the document."));
        }
        item.pageIndex = PDFInteger(*page) - 1;
        const QRectF mediaBox = document->getCatalog()->getPage(size_t(item.pageIndex))->getMediaBox().normalized();

        const std::optional<PDFMeasureKind> kind = getKind(object.value(QStringLiteral("kind")).toString());
        if (!kind)
        {
            return fail(prefix + tr("the kind is not distance, perimeter, area or angle."));
        }
        item.kind = *kind;

        const QJsonValue pointsValue = object.value(QStringLiteral("points"));
        // The closing point of a shape may repeat the first one
        if (!pointsValue.isArray() || pointsValue.toArray().size() > MAXIMUM_POINT_COUNT + 1)
        {
            return fail(prefix + tr("the points are not an array of at most %1 points.").arg(MAXIMUM_POINT_COUNT));
        }
        std::vector<QPointF> points;
        for (const QJsonValue& pointValue : pointsValue.toArray())
        {
            const QJsonArray coordinates = pointValue.toArray();
            const std::optional<PDFReal> x = coordinates.size() == 2 ? Helper::getNumber(coordinates.at(0)) : std::nullopt;
            const std::optional<PDFReal> y = coordinates.size() == 2 ? Helper::getNumber(coordinates.at(1)) : std::nullopt;
            if (!pointValue.isArray() || !x || !y)
            {
                return fail(prefix + tr("a point is not an array [x, y] of two numbers."));
            }

            // Review RS7CCORE L4: a measurement lies on its page - an annotation outside of the MediaBox (ISO
            // 32000-2, 14.11.2: the boundaries of the physical medium) would be measured, but never seen
            const QPointF point(*x, *y);
            if (!mediaBox.contains(point))
            {
                return fail(prefix + tr("the point [%1, %2] lies outside of the page (its MediaBox is [%3, %4, %5, %6]).")
                                         .arg(QString::number(*x), QString::number(*y), QString::number(mediaBox.left()), QString::number(mediaBox.top()),
                                              QString::number(mediaBox.right()), QString::number(mediaBox.bottom())));
            }
            points.push_back(point);
        }

        std::optional<std::vector<QPointF>> normalizedPoints = PDFMeasureGeometry::normalizePoints(item.kind, points, MAXIMUM_POINT_COUNT);
        if (!normalizedPoints)
        {
            return fail(prefix + tr("the points do not fit the kind (a distance has 2 points, an angle 3 with arms of a non-zero length, a perimeter and an area at least 3 different corners)."));
        }
        item.points = std::move(*normalizedPoints);

        const QJsonValue scaleValue = object.value(QStringLiteral("scale"));
        if (!scaleValue.isUndefined() && !scaleValue.isNull())
        {
            item.scale = Helper::getScale(scaleValue);
            if (!item.scale)
            {
                return fail(prefix + tr("the scale is not valid (positive values and the length units pt, in, mm, cm, m, ft, yd, km, mi)."));
            }
        }
        items.push_back(std::move(item));
    }

    return items;
}

QString PDFDocumentToolMeasure::toJson(const std::vector<PDFMeasureItem>& items)
{
    QJsonArray array;
    for (const PDFMeasureItem& item : items)
    {
        QJsonObject object;
        object.insert(QStringLiteral("page"), qint64(item.pageIndex + 1));
        object.insert(QStringLiteral("kind"), getKindId(item.kind));
        QJsonArray points;
        for (const QPointF& point : item.points)
        {
            points.append(QJsonArray({ point.x(), point.y() }));
        }
        object.insert(QStringLiteral("points"), points);
        if (item.scale)
        {
            QJsonObject scale;
            scale.insert(QStringLiteral("paperValue"), item.scale->getPaperValue());
            scale.insert(QStringLiteral("paperUnit"), QString::fromLatin1(item.scale->getPaperUnitId()));
            scale.insert(QStringLiteral("realValue"), item.scale->getRealValue());
            scale.insert(QStringLiteral("realUnit"), QString::fromLatin1(item.scale->getRealUnitId()));
            if (!item.scale->getName().isEmpty())
            {
                scale.insert(QStringLiteral("name"), item.scale->getName());
            }
            object.insert(QStringLiteral("scale"), scale);
        }
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

PDFMeasureScale PDFDocumentToolMeasure::getEffectiveScale(const PDFDocument* document, const PDFMeasureItem& item, QString* viewportName)
{
    if (viewportName)
    {
        viewportName->clear();
    }

    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (catalog && item.pageIndex >= 0 && size_t(item.pageIndex) < catalog->getPageCount() && !item.points.empty())
    {
        // 12.9.2 NOTE 1: a measurement of more viewports uses the viewport of its first point
        const PDFPage* page = catalog->getPage(size_t(item.pageIndex));
        const std::vector<PDFViewport> viewports = PDFViewport::parseViewports(&document->getStorage(), page->getViewports(&document->getStorage()));
        PDFMeasureScale viewportScale;
        if (const PDFViewport* viewport = PDFMeasureGeometry::findScaleViewport(viewports, item.points.front(), page->getEffectiveUserUnit(), &viewportScale))
        {
            if (viewportName)
            {
                *viewportName = viewport->getName();
            }
            return viewportScale;
        }
    }

    return item.scale && item.scale->isValid() ? *item.scale : PDFMeasureScale::createIdentity();
}

QJsonObject PDFDocumentToolMeasure::createData(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const std::vector<PDFMeasureItem>& items, const PDFMeasureDisplayUnits& units,
                                               const PDFOperationControl* control)
{
    bool isTruncated = false;
    const std::vector<PDFMeasureAnnotationInfo> annotations = PDFMeasureAnnotationInfo::readPages(document, pageIndices, MAXIMUM_LISTED_MEASUREMENTS, PDFMeasureAnnotationInfo::MAXIMUM_READ_POINT_COUNT,
                                                                                                  &isTruncated, control);
    return createData(document, pageIndices, annotations, isTruncated, items, units, control);
}

QJsonObject PDFDocumentToolMeasure::createData(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const std::vector<PDFMeasureAnnotationInfo>& annotations, bool isAnnotationsTruncated,
                                               const std::vector<PDFMeasureItem>& items, const PDFMeasureDisplayUnits& units, const PDFOperationControl* control)
{
    using Helper = PDFDocumentToolMeasureHelper;

    // The rows by their pages: the existing annotations first, then the new measurements (stable)
    std::vector<std::pair<PDFInteger, QJsonObject>> rows;
    bool isTruncated = isAnnotationsTruncated;

    for (const PDFMeasureAnnotationInfo& annotation : annotations)
    {
        QJsonObject row = Helper::createRow(annotation.pageIndex, annotation.kind, annotation.value, annotation.unitId.isEmpty() ? annotation.unitLabel : QString::fromLatin1(annotation.unitId),
                                            annotation.unitLabel, annotation.text, annotation.ratio, false);
        row.insert(QStringLiteral("annotation"), QStringLiteral("%1 %2 R").arg(annotation.annotation.objectNumber).arg(annotation.annotation.generation));
        rows.emplace_back(annotation.pageIndex, std::move(row));
    }

    for (const PDFMeasureItem& item : items)
    {
        if (rows.size() >= size_t(MAXIMUM_LISTED_MEASUREMENTS))
        {
            isTruncated = true;
            break;
        }

        QString viewportName;
        const PDFMeasureScale scale = getEffectiveScale(document, item, &viewportName);
        const PDFMeasureUnit unit = Helper::getUnit(item.kind, units);
        const PDFReal value = PDFMeasureGeometry::convertValue(item.kind, PDFMeasureGeometry::getValue(item.kind, item.points, Helper::getUserUnit(document, item.pageIndex)), scale, unit);
        const QString text = PDFMeasureGeometry::getValueText(item.kind, PDFMeasureGeometry::formatValue(value), unit.getDocumentSymbol());
        QJsonObject row = Helper::createRow(item.pageIndex, item.kind, value, QString::fromLatin1(unit.id), unit.getDocumentSymbol(), text, scale.getDocumentRatioText(), true);
        row.insert(QStringLiteral("viewport"), viewportName);
        rows.emplace_back(item.pageIndex, std::move(row));
    }

    std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) { return left.first < right.first; });

    QJsonArray measurements;
    for (auto& row : rows)
    {
        measurements.append(std::move(row.second));
    }

    // The viewports of the pages (Table 265): a /GEO one is listed and ignored for the measurements (FM-7)
    QJsonArray viewportsData;
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    for (const PDFInteger pageIndex : pageIndices)
    {
        if (PDFOperationControl::isOperationCanceled(control))
        {
            break;
        }
        if (!catalog || pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
        {
            continue;
        }
        const PDFPage* page = catalog->getPage(size_t(pageIndex));
        for (const PDFViewport& viewport : PDFViewport::parseViewports(&document->getStorage(), page->getViewports(&document->getStorage())))
        {
            if (viewportsData.size() >= MAXIMUM_LISTED_MEASUREMENTS)
            {
                isTruncated = true;
                break;
            }
            const QRectF box = viewport.getBoundingBox();
            QJsonObject row;
            row.insert(QStringLiteral("page"), qint64(pageIndex + 1));
            row.insert(QStringLiteral("name"), viewport.getName());
            row.insert(QStringLiteral("bbox"), QJsonArray({ box.left(), box.top(), box.right(), box.bottom() }));
            row.insert(QStringLiteral("ratio"), viewport.getMeasure().getScaleRatio());
            QString subtype;
            if (viewport.getMeasure().isValid())
            {
                subtype = viewport.getMeasure().isRectilinear() ? QStringLiteral("RL") : QStringLiteral("GEO");
            }
            row.insert(QStringLiteral("subtype"), subtype);
            viewportsData.append(row);
        }
    }

    QJsonObject data;
    data.insert(QString::fromLatin1(DATA_MEASUREMENTS), measurements);
    data.insert(QString::fromLatin1(DATA_VIEWPORTS), viewportsData);
    data.insert(QString::fromLatin1(DATA_TRUNCATED), isTruncated);
    return data;
}

std::optional<PDFDocument> PDFDocumentToolMeasure::addAnnotations(const PDFDocument* document, const std::vector<PDFMeasureItem>& items, const PDFMeasureDisplayUnits& units, const PDFMeasureAnnotationStyle& style,
                                                                  int* added, QJsonArray* failed, QString* errorMessage)
{
    if (added)
    {
        *added = 0;
    }
    if (!document)
    {
        if (errorMessage)
        {
            *errorMessage = tr("There is no document.");
        }
        return std::nullopt;
    }

    // Owner 2026-11-19 (review RS7CCORE M3): PDF/A-1 allows no transparency (ISO 19005-1, 6.4) - a document, which
    // declares PDF/A, gets opaque annotations (as the Signature tool decides); its declaration stays true
    PDFMeasureAnnotationStyle writtenStyle = style;
    writtenStyle.isOpaqueRequired = writtenStyle.isOpaqueRequired || getConformanceDeclarations(document).contains(QStringLiteral("PDF/A"));

    // One builder: every annotation of the call is one change (one step of Undo)
    PDFDocumentBuilder builder(document);
    const PDFCatalog* catalog = document->getCatalog();
    int addedCount = 0;
    QString firstError;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const PDFMeasureItem& item = items[i];
        QString reason;
        PDFObjectReference annotation;
        if (item.pageIndex >= 0 && size_t(item.pageIndex) < catalog->getPageCount())
        {
            const PDFMeasureScale scale = getEffectiveScale(document, item, nullptr);
            annotation = PDFMeasureWriter::createAnnotation(&builder, catalog->getPage(size_t(item.pageIndex))->getPageReference(), item.kind, item.points, scale, units, writtenStyle, &reason);
        }
        else
        {
            reason = tr("The page of the measurement does not exist.");
        }

        if (annotation.isValid())
        {
            ++addedCount;
        }
        else
        {
            if (firstError.isEmpty())
            {
                firstError = reason;
            }
            if (failed)
            {
                QJsonObject failure;
                failure.insert(QStringLiteral("index"), qint64(i));
                failure.insert(QStringLiteral("reason"), reason);
                failed->append(failure);
            }
        }
    }

    if (added)
    {
        *added = addedCount;
    }
    if (addedCount == 0)
    {
        if (errorMessage)
        {
            *errorMessage = firstError.isEmpty() ? tr("There is no measurement to add.") : firstError;
        }
        return std::nullopt;
    }
    return builder.build();
}

PDFMeasureDisplayUnits PDFDocumentToolMeasure::getUnits(const QVariantMap& parameters)
{
    PDFMeasureDisplayUnits units;
    units.length = parameters.value(QString::fromLatin1(PARAMETER_LENGTH_UNIT), QString::fromLatin1(PDFMeasureUnits::INITIAL_LENGTH_UNIT)).toString().toLatin1();
    units.area = parameters.value(QString::fromLatin1(PARAMETER_AREA_UNIT), QString::fromLatin1(PDFMeasureUnits::INITIAL_AREA_UNIT)).toString().toLatin1();
    units.angle = parameters.value(QString::fromLatin1(PARAMETER_ANGLE_UNIT), QString::fromLatin1(PDFMeasureUnits::INITIAL_ANGLE_UNIT)).toString().toLatin1();
    return units;
}

PDFMeasureAnnotationStyle PDFDocumentToolMeasure::getStyle(const QVariantMap& parameters, const QString& author)
{
    using Helper = PDFDocumentToolMeasureHelper;

    PDFMeasureAnnotationStyle style;
    style.fontFamily = parameters.value(QString::fromLatin1(PARAMETER_FONT_FAMILY), style.fontFamily).toString();
    bool isOk = false;
    const PDFReal fontSize = parameters.value(QString::fromLatin1(PARAMETER_FONT_SIZE)).toDouble(&isOk);
    if (isOk && std::isfinite(fontSize))
    {
        style.fontSize = qBound(4.0, fontSize, 144.0);
    }
    style.isBold = parameters.value(QString::fromLatin1(PARAMETER_BOLD), style.isBold).toBool();
    style.isItalic = parameters.value(QString::fromLatin1(PARAMETER_ITALIC), style.isItalic).toBool();
    style.textColor = Helper::getColor(parameters, PARAMETER_TEXT_COLOR, PARAMETER_TEXT_OPACITY, Qt::black, DEFAULT_TEXT_OPACITY);
    style.backgroundColor = Helper::getColor(parameters, PARAMETER_BACKGROUND_COLOR, PARAMETER_BACKGROUND_OPACITY, Qt::black, DEFAULT_BACKGROUND_OPACITY);

    // "The colors apply to both" (471:5490): the lines of a measurement have the color of its text, opaque
    style.lineColor = QColor(style.textColor.red(), style.textColor.green(), style.textColor.blue());
    style.author = author;
    return style;
}

bool PDFDocumentToolMeasure::isAnnotationAllowedByCertification(const PDFDocument* document)
{
    const PDFDocMDPPermissions::Level level = PDFDocMDPPermissions::getLevel(document);
    return level == PDFDocMDPPermissions::Level::NotCertified || level == PDFDocMDPPermissions::Level::FormFillingAndAnnotations;
}

PDFDocumentToolTable PDFDocumentToolMeasure::createTable(const QJsonObject& data)
{
    using ValueType = PDFDocumentToolTableValueType;

    // Q7.20, 473:91038: Page; Type; Value; Unit; Text; Scale - every measurement of the data, the new and the
    // existing ones; the texts of an annotation come from the document (the formula escape of B182)
    PDFDocumentToolTable table;
    table.name = QStringLiteral("measurements");
    table.title = tr("Measurements");
    table.addColumn(QStringLiteral("page"), tr("Page"), ValueType::Integer);
    table.addColumn(QStringLiteral("type"), tr("Type"), ValueType::Text);
    table.addColumn(QStringLiteral("value"), tr("Value"), ValueType::Real, 4);
    table.addColumn(QStringLiteral("unit"), tr("Unit"), ValueType::DocumentText);
    table.addColumn(QStringLiteral("text"), tr("Text"), ValueType::DocumentText);
    table.addColumn(QStringLiteral("scale"), tr("Scale"), ValueType::DocumentText);

    for (const QJsonValue& rowValue : data.value(QString::fromLatin1(DATA_MEASUREMENTS)).toArray())
    {
        const QJsonObject row = rowValue.toObject();
        const std::optional<PDFMeasureKind> kind = getKind(row.value(QStringLiteral("kind")).toString());
        const QJsonValue value = row.value(QStringLiteral("value"));
        table.addRow({ QVariant(row.value(QStringLiteral("page")).toInteger()),
                       QVariant(kind ? PDFMeasureGeometry::getKindTitle(*kind) : QString()),
                       value.isDouble() ? QVariant(value.toDouble()) : QVariant(),
                       QVariant(row.value(QStringLiteral("unitLabel")).toString()),
                       QVariant(row.value(QStringLiteral("text")).toString()),
                       QVariant(row.value(QStringLiteral("scale")).toString()) });
    }
    return table;
}

PDFDocumentToolOutputFile PDFDocumentToolMeasure::writeOutput(const QJsonObject& data, const QVariantMap& parameters, const PDFDocumentToolTableWriter::Options& options, const QString& baseName, QString* errorMessage)
{
    Q_UNUSED(parameters);

    // The export of Measure is always one table as CSV (473:91038: "CSV, semicolon separated, UTF-8")
    PDFDocumentToolTableSection document;
    document.name = QStringLiteral("measure");
    document.title = tr("Measurements");
    document.addTable(createTable(data));
    return PDFDocumentToolTableWriter::createOutput(document, PDFDocumentToolTableFormat::Csv, options, baseName, errorMessage);
}

}   // namespace pdf
