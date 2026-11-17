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

#include "pdfdocumenttool_statistics.h"
#include "pdfcatalog.h"
#include "pdfdocument.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>

#include <algorithm>

namespace pdf
{

/// Helpers of Document Statistics
class PDFDocumentToolStatisticsHelper
{
public:
    PDFDocumentToolStatisticsHelper() = delete;

    static QString name(const char* text) { return QString::fromLatin1(text); }

    /// The share of a part in percents; 0 of nothing (B186: the old dialog divided by zero)
    static double getShare(qint64 part, qint64 total)
    {
        return total > 0 ? 100.0 * double(part) / double(total) : 0.0;
    }

    /// The rows of \p key of the data in their order
    static std::vector<QJsonObject> getRows(const QJsonObject& data, const char* key)
    {
        std::vector<QJsonObject> rows;
        for (const QJsonValue& value : data.value(name(key)).toArray())
        {
            rows.push_back(value.toObject());
        }
        return rows;
    }
};

QString PDFDocumentToolStatistics::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolStatistics::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    std::vector<PDFDocumentToolParameterOption> views;
    views.push_back({ QString::fromLatin1(VIEW_FUNCTION), QT_TR_NOOP("By Function"), QT_TR_NOOP("The objects by what they are used for") });
    views.push_back({ QString::fromLatin1(VIEW_TYPE), QT_TR_NOOP("By Object Type"), QT_TR_NOOP("The objects by their type of PDF") });
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_VIEW), QT_TR_NOOP("View"), QT_TR_NOOP("The table of a CSV export"), std::move(views), QString::fromLatin1(VIEW_FUNCTION)));
    definitions.push_back(PDFDocumentToolTableWriter::createFormatParameter({ PDFDocumentToolTableFormat::None, PDFDocumentToolTableFormat::Csv, PDFDocumentToolTableFormat::Text,
                                                                              PDFDocumentToolTableFormat::Html, PDFDocumentToolTableFormat::Xml }, PDFDocumentToolTableFormat::None));
    return definitions;
}

PDFDocumentToolResult PDFDocumentToolStatistics::computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    return execute(context, parameters, control, progress);
}

QString PDFDocumentToolStatistics::getClassId(PDFObjectClassifier::Type type)
{
    switch (type)
    {
        case PDFObjectClassifier::Page:
            return QStringLiteral("page");
        case PDFObjectClassifier::ContentStream:
            return QStringLiteral("content-stream");
        case PDFObjectClassifier::GraphicState:
            return QStringLiteral("graphic-state");
        case PDFObjectClassifier::ColorSpace:
            return QStringLiteral("color-space");
        case PDFObjectClassifier::Pattern:
            return QStringLiteral("pattern");
        case PDFObjectClassifier::Shading:
            return QStringLiteral("shading");
        case PDFObjectClassifier::Image:
            return QStringLiteral("image");
        case PDFObjectClassifier::Form:
            return QStringLiteral("form");
        case PDFObjectClassifier::Font:
            return QStringLiteral("font");
        case PDFObjectClassifier::Action:
            return QStringLiteral("action");
        case PDFObjectClassifier::Annotation:
            return QStringLiteral("annotation");
        case PDFObjectClassifier::FileStructure:
            return QStringLiteral("file-structure");
        case PDFObjectClassifier::None:
            break;
    }
    return QStringLiteral("other");
}

QString PDFDocumentToolStatistics::getClassTitle(const QString& id)
{
    // The titles of the rows of 480:14219 (the old dialog: Page, Content Stream, ..., Other) and the new
    // row of the containers of the file structure (B206)
    static const std::map<QString, const char*> titles = {
        { QStringLiteral("page"), QT_TR_NOOP("Page") },
        { QStringLiteral("content-stream"), QT_TR_NOOP("Content Stream") },
        { QStringLiteral("graphic-state"), QT_TR_NOOP("Graphic State") },
        { QStringLiteral("color-space"), QT_TR_NOOP("Color Space") },
        { QStringLiteral("pattern"), QT_TR_NOOP("Pattern") },
        { QStringLiteral("shading"), QT_TR_NOOP("Shading") },
        { QStringLiteral("image"), QT_TR_NOOP("Image") },
        { QStringLiteral("form"), QT_TR_NOOP("Form") },
        { QStringLiteral("font"), QT_TR_NOOP("Font") },
        { QStringLiteral("action"), QT_TR_NOOP("Action") },
        { QStringLiteral("annotation"), QT_TR_NOOP("Annotation") },
        { QStringLiteral("file-structure"), QT_TR_NOOP("File Structure") },
        { QStringLiteral("other"), QT_TR_NOOP("Other") }
    };

    auto it = titles.find(id);
    return it != titles.cend() ? tr(it->second) : id;
}

QString PDFDocumentToolStatistics::getTypeId(PDFObject::Type type)
{
    switch (type)
    {
        case PDFObject::Type::Null:
            return QStringLiteral("null");
        case PDFObject::Type::Bool:
            return QStringLiteral("boolean");
        case PDFObject::Type::Int:
            return QStringLiteral("integer");
        case PDFObject::Type::Real:
            return QStringLiteral("real");
        case PDFObject::Type::String:
            return QStringLiteral("string");
        case PDFObject::Type::Name:
            return QStringLiteral("name");
        case PDFObject::Type::Array:
            return QStringLiteral("array");
        case PDFObject::Type::Dictionary:
            return QStringLiteral("dictionary");
        case PDFObject::Type::Stream:
            return QStringLiteral("stream");
        case PDFObject::Type::Reference:
            return QStringLiteral("reference");
        default:
            break;
    }
    return QString();
}

QString PDFDocumentToolStatistics::getTypeTitle(const QString& id)
{
    // The titles of 480:14555
    static const std::map<QString, const char*> titles = {
        { QStringLiteral("null"), QT_TR_NOOP("Null") },
        { QStringLiteral("boolean"), QT_TR_NOOP("Boolean") },
        { QStringLiteral("integer"), QT_TR_NOOP("Integer") },
        { QStringLiteral("real"), QT_TR_NOOP("Real") },
        { QStringLiteral("string"), QT_TR_NOOP("String") },
        { QStringLiteral("name"), QT_TR_NOOP("Name") },
        { QStringLiteral("array"), QT_TR_NOOP("Array") },
        { QStringLiteral("dictionary"), QT_TR_NOOP("Dictionary") },
        { QStringLiteral("stream"), QT_TR_NOOP("Stream") },
        { QStringLiteral("reference"), QT_TR_NOOP("Reference") }
    };

    auto it = titles.find(id);
    return it != titles.cend() ? tr(it->second) : id;
}

QJsonObject PDFDocumentToolStatistics::createData(const PDFDocument* document, qint64 fileSize, bool isRepaired, const PDFOperationControl* control, bool* isCanceled)
{
    using Helper = PDFDocumentToolStatisticsHelper;

    *isCanceled = false;
    QJsonObject data;
    if (!document)
    {
        return data;
    }

    PDFObjectClassifier classifier;
    if (!classifier.classify(document, control))
    {
        *isCanceled = true;
        return QJsonObject();
    }

    const PDFObjectClassifier::Statistics statistics = classifier.calculateStatistics(document, control);
    if (statistics.isCanceled)
    {
        *isCanceled = true;
        return QJsonObject();
    }

    qint64 totalBytes = 0;
    qint64 objectCount = 0;
    for (const auto& item : statistics.statistics)
    {
        totalBytes += item.second.bytes.load();
        objectCount += item.second.count.load();
    }

    // The classes by their bytes, the largest first (480:14219); equal ones in the order of the classes
    std::vector<std::pair<qint64, QJsonObject>> classRows;
    for (const PDFObjectClassifier::Type type : PDFObjectClassifier::getClasses())
    {
        auto it = statistics.statistics.find(type);
        if (it == statistics.statistics.cend() || it->second.count.load() == 0)
        {
            continue;
        }

        const qint64 bytes = it->second.bytes.load();
        QJsonObject row;
        row.insert(Helper::name(ROW_ID), getClassId(type));
        row.insert(Helper::name(ROW_COUNT), qint64(it->second.count.load()));
        row.insert(Helper::name(ROW_BYTES), bytes);
        row.insert(Helper::name(ROW_SHARE), Helper::getShare(bytes, totalBytes));
        classRows.emplace_back(bytes, row);
    }
    std::stable_sort(classRows.begin(), classRows.end(), [](const auto& left, const auto& right) { return left.first > right.first; });

    // The PDF types by their count (480:14555), the share of the values (K7.13)
    qint64 valueCount = 0;
    for (const PDFObject::Type type : PDFObject::getTypes())
    {
        valueCount += statistics.objectCountByType[size_t(type)];
    }

    std::vector<std::pair<qint64, QJsonObject>> typeRows;
    for (const PDFObject::Type type : PDFObject::getTypes())
    {
        const qint64 count = statistics.objectCountByType[size_t(type)];
        if (count == 0)
        {
            continue;
        }

        QJsonObject row;
        row.insert(Helper::name(ROW_TYPE), getTypeId(type));
        row.insert(Helper::name(ROW_COUNT), count);
        row.insert(Helper::name(ROW_SHARE), Helper::getShare(count, valueCount));
        typeRows.emplace_back(count, row);
    }
    std::stable_sort(typeRows.begin(), typeRows.end(), [](const auto& left, const auto& right) { return left.first > right.first; });

    QJsonArray classes;
    for (const auto& row : classRows)
    {
        classes.append(row.second);
    }
    QJsonArray types;
    for (const auto& row : typeRows)
    {
        types.append(row.second);
    }

    data.insert(Helper::name(DATA_CLASSES), classes);
    data.insert(Helper::name(DATA_TYPES), types);
    data.insert(Helper::name(DATA_FILE_SIZE), qMax(fileSize, qint64(-1)));
    data.insert(Helper::name(DATA_VERSION), QString::fromLatin1(document->getVersion()));
    data.insert(Helper::name(DATA_OBJECT_COUNT), objectCount);
    data.insert(Helper::name(DATA_UNSAVED), fileSize < 0);
    data.insert(Helper::name(DATA_COMPLETE), classifier.isComplete());
    data.insert(Helper::name(DATA_TOTAL_BYTES), totalBytes);
    data.insert(Helper::name(DATA_VALUE_COUNT), valueCount);
    data.insert(Helper::name(DATA_PAGE_COUNT), qint64(document->getCatalog()->getPageCount()));
    data.insert(Helper::name(DATA_REPAIRED), isRepaired);
    return data;
}

PDFDocumentToolResult PDFDocumentToolStatistics::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolStatisticsHelper;
    Q_UNUSED(progress);

    const PDFDocument* document = context.getDocument();
    if (!document)
    {
        return PDFDocumentToolResult::createFailed(tr("There is no document."));
    }

    // FD-2: the size of the file is known only when the document is the file (no unsaved changes)
    const PDFDocumentToolInputDocument& input = context.documents.front();
    const qint64 fileSize = (!input.isModified && !input.originalData.isEmpty()) ? qint64(input.originalData.size()) : qint64(-1);

    // Plan 4.2, FD-7: the one source of the repair is the input (PDFAppDocument::getRestoration of the shell)
    bool isCanceled = false;
    const QJsonObject data = createData(document, fileSize, input.isRestored, control, &isCanceled);
    if (isCanceled)
    {
        return PDFDocumentToolResult::createStopped();
    }

    PDFDocumentToolResult result;
    result.data = data;

    // The facts are machine-readable (MCP): the C locale; the GUI makes its own by PDFAppLocale
    const QLocale locale = QLocale::c();
    result.facts.push_back({ QStringLiteral("objects"), tr("Objects"), locale.toString(data.value(Helper::name(DATA_OBJECT_COUNT)).toInteger()) });
    result.facts.push_back({ QStringLiteral("objectBytes"), tr("Object bytes"), PDFDocumentToolFormat::formatSize(data.value(Helper::name(DATA_TOTAL_BYTES)).toInteger(), locale) });
    if (fileSize >= 0)
    {
        result.facts.push_back({ QStringLiteral("fileSize"), tr("File size"), PDFDocumentToolFormat::formatSize(fileSize, locale) });
    }
    if (!data.value(Helper::name(DATA_COMPLETE)).toBool())
    {
        result.facts.push_back({ QStringLiteral("incomplete"), tr("Incomplete"), tr("Not all objects were classified.") });
    }
    if (data.value(Helper::name(DATA_REPAIRED)).toBool())
    {
        result.facts.push_back({ QStringLiteral("repaired"), tr("Repaired when opened"), tr("Yes") });
    }

    if (PDFDocumentToolTableWriter::getFormat(parameters) != PDFDocumentToolTableFormat::None)
    {
        const QString baseName = QFileInfo(input.fileName).completeBaseName();
        QString errorMessage;
        PDFDocumentToolOutputFile output = writeOutput(data, parameters, PDFDocumentToolTableWriter::Options(), (baseName.isEmpty() ? QStringLiteral("document") : baseName) + QStringLiteral(" - statistics"), &errorMessage);
        if (!errorMessage.isEmpty())
        {
            return PDFDocumentToolResult::createFailed(errorMessage);
        }
        result.outputs.push_back(std::move(output));
    }

    return result;
}

PDFDocumentToolOutputFile PDFDocumentToolStatistics::writeOutput(const QJsonObject& data, const QVariantMap& parameters, const PDFDocumentToolTableWriter::Options& options, const QString& baseName, QString* errorMessage)
{
    using Helper = PDFDocumentToolStatisticsHelper;
    using ValueType = PDFDocumentToolTableValueType;

    const PDFDocumentToolTableFormat format = PDFDocumentToolTableWriter::getFormat(parameters);
    if (format == PDFDocumentToolTableFormat::None)
    {
        if (errorMessage)
        {
            *errorMessage = tr("No format of the export is chosen.");
        }
        return PDFDocumentToolOutputFile();
    }

    // The texts of the paragraphs by the formatter of Text and HTML, the C locale for XML (the writer
    // formats the cells itself)
    const bool isReadable = format == PDFDocumentToolTableFormat::Text || format == PDFDocumentToolTableFormat::Html;
    const PDFDocumentToolValueFormatter* formatter = (isReadable && options.formatter) ? options.formatter : PDFDocumentToolValueFormatter::getCLocale();

    PDFDocumentToolTable byFunction;
    byFunction.name = QStringLiteral("byFunction");
    byFunction.title = tr("Statistics by object function");
    byFunction.addColumn(QStringLiteral("class"), tr("Class"), ValueType::Text);
    byFunction.addColumn(QStringLiteral("share"), tr("Share (%)"), ValueType::Real, 2);
    byFunction.addColumn(QStringLiteral("count"), tr("Count"), ValueType::Integer);
    byFunction.addColumn(QStringLiteral("bytes"), tr("Size (bytes)"), ValueType::Integer);
    for (const QJsonObject& row : Helper::getRows(data, DATA_CLASSES))
    {
        byFunction.addRow({ getClassTitle(row.value(Helper::name(ROW_ID)).toString()), row.value(Helper::name(ROW_SHARE)).toDouble(),
                            row.value(Helper::name(ROW_COUNT)).toInteger(), row.value(Helper::name(ROW_BYTES)).toInteger() });
    }

    PDFDocumentToolTable byType;
    byType.name = QStringLiteral("byType");
    byType.title = tr("Statistics by object type");
    byType.addColumn(QStringLiteral("type"), tr("Type"), ValueType::Text);
    byType.addColumn(QStringLiteral("share"), tr("Share (%)"), ValueType::Real, 2);
    byType.addColumn(QStringLiteral("count"), tr("Count"), ValueType::Integer);
    for (const QJsonObject& row : Helper::getRows(data, DATA_TYPES))
    {
        byType.addRow({ getTypeTitle(row.value(Helper::name(ROW_TYPE)).toString()), row.value(Helper::name(ROW_SHARE)).toDouble(), row.value(Helper::name(ROW_COUNT)).toInteger() });
    }

    PDFDocumentToolTableSection document;
    document.name = QStringLiteral("statistics");
    document.title = tr("Document Statistics");

    if (format == PDFDocumentToolTableFormat::Csv)
    {
        // K7.20: CSV is one table of RFC 4180 - the table of the current view
        const bool isTypeView = parameters.value(Helper::name(PARAMETER_VIEW)).toString() == QLatin1String(VIEW_TYPE);
        document.addTable(isTypeView ? std::move(byType) : std::move(byFunction));
    }
    else
    {
        const qint64 fileSize = data.value(Helper::name(DATA_FILE_SIZE)).toInteger(-1);
        document.addParagraph(tr("PDF version: %1").arg(data.value(Helper::name(DATA_VERSION)).toString()));
        document.addParagraph(tr("Pages: %1").arg(formatter->formatInteger(data.value(Helper::name(DATA_PAGE_COUNT)).toInteger())));
        document.addParagraph(tr("Objects: %1").arg(formatter->formatInteger(data.value(Helper::name(DATA_OBJECT_COUNT)).toInteger())));
        document.addParagraph(tr("Object bytes: %1").arg(formatter->formatInteger(data.value(Helper::name(DATA_TOTAL_BYTES)).toInteger())));
        document.addParagraph(fileSize >= 0 ? tr("File size: %1 bytes").arg(formatter->formatInteger(fileSize))
                                            : tr("File size: unknown, the document has unsaved changes."));
        if (data.value(Helper::name(DATA_REPAIRED)).toBool())
        {
            // Plan 4.2, FD-7: the file is damaged, the statistics are of the objects, which were restored
            document.addParagraph(tr("Repaired when opened: the file is damaged, the statistics are of the objects read from it."));
        }
        if (!data.value(Helper::name(DATA_COMPLETE)).toBool(true))
        {
            document.addParagraph(tr("Not all objects were classified: the document exceeds a limit of the walk over its objects."));
        }
        document.addTable(std::move(byFunction));
        document.addTable(std::move(byType));
    }

    return PDFDocumentToolTableWriter::createOutput(document, format, options, baseName, errorMessage);
}

}   // namespace pdf
