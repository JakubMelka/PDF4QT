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

#include "pdfdocumenttooltablewriter.h"
#include "pdfdocumenttextexporter.h"
#include "pdfoutputformatter.h"

#include <QLocale>

#include <cmath>

namespace pdf
{

/// The parts of the writer, which walk the model
class PDFDocumentToolTableWriterHelper
{
public:
    PDFDocumentToolTableWriterHelper() = delete;

    /// The table of CSV: the only table of the model (nullptr, when it has none or more of them)
    static const PDFDocumentToolTable* findOnlyTable(const PDFDocumentToolTableSection& section, int depth, int* count)
    {
        const PDFDocumentToolTable* found = nullptr;
        if (depth > PDFDocumentToolTableWriter::MAXIMUM_SECTION_DEPTH)
        {
            return nullptr;
        }

        for (const PDFDocumentToolTableItem& item : section.items)
        {
            if (item.kind == PDFDocumentToolTableItem::Kind::Table)
            {
                ++*count;
                found = &item.table;
            }
            else if (item.kind == PDFDocumentToolTableItem::Kind::Section && item.sectionIndex >= 0 && size_t(item.sectionIndex) < section.sections.size())
            {
                if (const PDFDocumentToolTable* nested = findOnlyTable(section.sections[size_t(item.sectionIndex)], depth + 1, count))
                {
                    found = nested;
                }
            }
        }
        return *count == 1 ? found : nullptr;
    }

    /// The deepest nesting of the sections (the root has 0)
    static int getDepth(const PDFDocumentToolTableSection& section, int depth)
    {
        int result = depth;
        if (depth > PDFDocumentToolTableWriter::MAXIMUM_SECTION_DEPTH)
        {
            return result;
        }

        for (const PDFDocumentToolTableSection& nested : section.sections)
        {
            result = qMax(result, getDepth(nested, depth + 1));
        }
        return result;
    }

    /// The text of a cell for reading (Text, HTML) or as data (XML)
    static QString getCellText(const QVariant& value, const PDFDocumentToolTableColumn& column, const PDFDocumentToolTableWriter::Options& options, bool isData)
    {
        if (!value.isValid() || value.isNull())
        {
            return QString();
        }

        const int decimals = qBound(0, column.decimals, 10);
        if (isData)
        {
            return PDFDocumentToolValueFormatter::formatData(value, column.type, decimals);
        }

        const PDFDocumentToolValueFormatter* formatter = options.formatter ? options.formatter : PDFDocumentToolValueFormatter::getCLocale();
        switch (column.type)
        {
            case PDFDocumentToolTableValueType::Text:
            case PDFDocumentToolTableValueType::DocumentText:
                return value.toString();
            case PDFDocumentToolTableValueType::Integer:
                return formatter->formatInteger(value.toLongLong());
            case PDFDocumentToolTableValueType::Real:
                return formatter->formatReal(value.toDouble(), decimals);
            case PDFDocumentToolTableValueType::DateTime:
                return formatter->formatDateTime(value.toDateTime(), options.dateFormat);
        }
        return QString();
    }

    static Qt::Alignment getAlignment(const PDFDocumentToolTableColumn& column)
    {
        return column.type == PDFDocumentToolTableValueType::Integer || column.type == PDFDocumentToolTableValueType::Real ? Qt::AlignRight : Qt::AlignLeft;
    }

    /// Writes the items of a section by the formatter (Text, HTML, XML); the texts of HTML and XML
    /// without the characters, which XML 1.0 does not allow
    static void writeSection(const PDFDocumentToolTableSection& section, int depth, PDFOutputFormatter& formatter, const PDFDocumentToolTableWriter::Options& options, bool isMarkup, bool isData)
    {
        for (const PDFDocumentToolTableItem& item : section.items)
        {
            switch (item.kind)
            {
                case PDFDocumentToolTableItem::Kind::Paragraph:
                    formatter.writeText(QStringLiteral("paragraph"), toText(item.text, isMarkup));
                    break;

                case PDFDocumentToolTableItem::Kind::Code:
                    formatter.writePreformatted(QStringLiteral("code"), toText(item.text, isMarkup));
                    break;

                case PDFDocumentToolTableItem::Kind::Table:
                    writeTable(item.table, formatter, options, isMarkup, isData);
                    break;

                case PDFDocumentToolTableItem::Kind::Section:
                {
                    if (item.sectionIndex < 0 || size_t(item.sectionIndex) >= section.sections.size() || depth >= PDFDocumentToolTableWriter::MAXIMUM_SECTION_DEPTH)
                    {
                        break;
                    }

                    const PDFDocumentToolTableSection& nested = section.sections[size_t(item.sectionIndex)];
                    formatter.beginHeader(PDFDocumentToolTableWriter::toXmlName(nested.name), toText(nested.title, isMarkup));
                    writeSection(nested, depth + 1, formatter, options, isMarkup, isData);
                    formatter.endHeader();
                    break;
                }
            }
        }
    }

    static void writeTable(const PDFDocumentToolTable& table, PDFOutputFormatter& formatter, const PDFDocumentToolTableWriter::Options& options, bool isMarkup, bool isData)
    {
        formatter.beginTable(PDFDocumentToolTableWriter::toXmlName(table.name), toText(table.title, isMarkup));

        // The header row is always written: a table of the text style needs one row at least
        formatter.beginTableHeaderRow(QStringLiteral("header"));
        for (const PDFDocumentToolTableColumn& column : table.columns)
        {
            formatter.writeTableHeaderColumn(PDFDocumentToolTableWriter::toXmlName(column.name), toText(column.title, isMarkup), getAlignment(column));
        }
        formatter.endTableHeaderRow();

        for (const std::vector<QVariant>& row : table.rows)
        {
            formatter.beginTableRow(QStringLiteral("row"));
            for (size_t i = 0; i < table.columns.size(); ++i)
            {
                const PDFDocumentToolTableColumn& column = table.columns[i];
                const QString text = i < row.size() ? getCellText(row[i], column, options, isData) : QString();
                formatter.writeTableColumn(PDFDocumentToolTableWriter::toXmlName(column.name), toText(text, isMarkup), getAlignment(column));
            }
            formatter.endTableRow();
        }

        formatter.endTable();
    }

    static QString toText(const QString& text, bool isMarkup)
    {
        return isMarkup ? PDFDocumentToolTableWriter::toXmlText(text) : text;
    }

    /// Text of a date in the C locale by the format
    static QString formatDate(const QDateTime& dateTime, PDFDocumentToolDateFormat format)
    {
        if (!dateTime.isValid())
        {
            return QString();
        }

        switch (format)
        {
            case PDFDocumentToolDateFormat::Short:
                return QLocale::c().toString(dateTime, QLocale::ShortFormat);
            case PDFDocumentToolDateFormat::Long:
                return QLocale::c().toString(dateTime, QLocale::LongFormat);
            case PDFDocumentToolDateFormat::Iso:
                return dateTime.toOffsetFromUtc(dateTime.offsetFromUtc()).toString(Qt::ISODate);
            case PDFDocumentToolDateFormat::Rfc2822:
                return dateTime.toString(Qt::RFC2822Date);
        }
        return QString();
    }
};

// ---------------------------------------------------------------- formatter

PDFDocumentToolValueFormatter::~PDFDocumentToolValueFormatter()
{

}

QString PDFDocumentToolValueFormatter::formatInteger(qint64 value) const
{
    return QString::number(value);
}

QString PDFDocumentToolValueFormatter::formatReal(double value, int decimals) const
{
    if (!std::isfinite(value))
    {
        return QString();
    }
    return QString::number(value, 'f', qBound(0, decimals, 10));
}

QString PDFDocumentToolValueFormatter::formatDateTime(const QDateTime& dateTime, PDFDocumentToolDateFormat format) const
{
    return PDFDocumentToolTableWriterHelper::formatDate(dateTime, format);
}

const PDFDocumentToolValueFormatter* PDFDocumentToolValueFormatter::getCLocale()
{
    static const PDFDocumentToolValueFormatter formatter;
    return &formatter;
}

QString PDFDocumentToolValueFormatter::formatData(const QVariant& value, PDFDocumentToolTableValueType type, int decimals)
{
    if (!value.isValid() || value.isNull())
    {
        return QString();
    }

    switch (type)
    {
        case PDFDocumentToolTableValueType::Text:
        case PDFDocumentToolTableValueType::DocumentText:
            return value.toString();
        case PDFDocumentToolTableValueType::Integer:
            return QString::number(value.toLongLong());
        case PDFDocumentToolTableValueType::Real:
        {
            const double number = value.toDouble();
            return std::isfinite(number) ? QString::number(number, 'f', qBound(0, decimals, 10)) : QString();
        }
        case PDFDocumentToolTableValueType::DateTime:
            return PDFDocumentToolTableWriterHelper::formatDate(value.toDateTime(), PDFDocumentToolDateFormat::Iso);
    }
    return QString();
}

// ---------------------------------------------------------------- model

void PDFDocumentToolTable::addColumn(QString columnName, QString columnTitle, PDFDocumentToolTableValueType type, int decimals)
{
    PDFDocumentToolTableColumn column;
    column.name = std::move(columnName);
    column.title = std::move(columnTitle);
    column.type = type;
    column.decimals = decimals;
    columns.push_back(std::move(column));
}

void PDFDocumentToolTableSection::addParagraph(QString paragraph)
{
    PDFDocumentToolTableItem item;
    item.kind = PDFDocumentToolTableItem::Kind::Paragraph;
    item.text = std::move(paragraph);
    items.push_back(std::move(item));
}

void PDFDocumentToolTableSection::addCode(QString code)
{
    PDFDocumentToolTableItem item;
    item.kind = PDFDocumentToolTableItem::Kind::Code;
    item.text = std::move(code);
    items.push_back(std::move(item));
}

void PDFDocumentToolTableSection::addTable(PDFDocumentToolTable table)
{
    PDFDocumentToolTableItem item;
    item.kind = PDFDocumentToolTableItem::Kind::Table;
    item.table = std::move(table);
    items.push_back(std::move(item));
}

PDFDocumentToolTableSection* PDFDocumentToolTableSection::addSection(QString sectionName, QString sectionTitle)
{
    PDFDocumentToolTableSection section;
    section.name = std::move(sectionName);
    section.title = std::move(sectionTitle);
    sections.push_back(std::move(section));

    PDFDocumentToolTableItem item;
    item.kind = PDFDocumentToolTableItem::Kind::Section;
    item.sectionIndex = int(sections.size()) - 1;
    items.push_back(std::move(item));
    return &sections.back();
}

int PDFDocumentToolTableSection::getTableCount() const
{
    int count = 0;
    PDFDocumentToolTableWriterHelper::findOnlyTable(*this, 0, &count);
    return count;
}

// ---------------------------------------------------------------- writer

PDFDocumentToolTableWriter::Result PDFDocumentToolTableWriter::write(const PDFDocumentToolTableSection& document, PDFDocumentToolTableFormat format, const Options& options)
{
    Result result;
    if (PDFDocumentToolTableWriterHelper::getDepth(document, 0) > MAXIMUM_SECTION_DEPTH)
    {
        result.errorMessage = tr("The export has sections nested too deeply.");
        return result;
    }

    PDFOutputFormatter::Style style = PDFOutputFormatter::Style::Text;
    switch (format)
    {
        case PDFDocumentToolTableFormat::None:
            result.errorMessage = tr("No format of the file was chosen.");
            return result;

        case PDFDocumentToolTableFormat::Csv:
        {
            const QChar separator = options.csvSeparator;
            if (separator == QLatin1Char('"') || separator == QLatin1Char('\r') || separator == QLatin1Char('\n') || separator.isNull())
            {
                result.errorMessage = tr("The separator of the fields cannot be used.");
                return result;
            }

            int count = 0;
            const PDFDocumentToolTable* table = PDFDocumentToolTableWriterHelper::findOnlyTable(document, 0, &count);
            if (!table)
            {
                result.errorMessage = count == 0 ? tr("There is no table to export.") : tr("A CSV file holds one table only; choose one view.");
                return result;
            }
            result.data = writeCsv(*table, separator);
            return result;
        }

        case PDFDocumentToolTableFormat::Text:
            style = PDFOutputFormatter::Style::Text;
            break;

        case PDFDocumentToolTableFormat::Html:
            style = PDFOutputFormatter::Style::Html;
            break;

        case PDFDocumentToolTableFormat::Xml:
            style = PDFOutputFormatter::Style::Xml;
            break;
    }

    const bool isMarkup = style != PDFOutputFormatter::Style::Text;
    const bool isData = style == PDFOutputFormatter::Style::Xml;
    PDFOutputFormatter formatter(style);
    formatter.beginDocument(toXmlName(document.name), PDFDocumentToolTableWriterHelper::toText(document.title, isMarkup));
    PDFDocumentToolTableWriterHelper::writeSection(document, 0, formatter, options, isMarkup, isData);
    formatter.endDocument();

    // The one encoding of the exports of text (UTF-8, the charset of a markup file written into it)
    result.data = PDFDocumentTextExporter::encode(formatter.getString(), style, PDFDocumentTextExporter::Encoding::Utf8).data;
    return result;
}

QByteArray PDFDocumentToolTableWriter::writeCsv(const PDFDocumentToolTable& table, QChar separator)
{
    QString text;
    const QString lineEnd = QStringLiteral("\r\n");

    QStringList fields;
    for (const PDFDocumentToolTableColumn& column : table.columns)
    {
        fields << toCsvField(column.title, false, separator);
    }
    text += fields.join(separator) + lineEnd;

    for (const std::vector<QVariant>& row : table.rows)
    {
        fields.clear();
        for (size_t i = 0; i < table.columns.size(); ++i)
        {
            const PDFDocumentToolTableColumn& column = table.columns[i];
            const QString value = i < row.size() ? PDFDocumentToolValueFormatter::formatData(row[i], column.type, column.decimals) : QString();
            fields << toCsvField(value, column.type == PDFDocumentToolTableValueType::DocumentText, separator);
        }
        text += fields.join(separator) + lineEnd;
    }

    // UTF-8 without a byte order mark
    return text.toUtf8();
}

PDFDocumentToolOutputFile PDFDocumentToolTableWriter::createOutput(const PDFDocumentToolTableSection& document, PDFDocumentToolTableFormat format, const Options& options, const QString& baseName, QString* errorMessage)
{
    PDFDocumentToolOutputFile output;
    const Result result = write(document, format, options);
    if (!result.isSuccess())
    {
        if (errorMessage)
        {
            *errorMessage = result.errorMessage;
        }
        return output;
    }

    output.suggestedFileName = QStringLiteral("%1.%2").arg(baseName.isEmpty() ? QStringLiteral("export") : baseName, getExtension(format));
    output.mimeType = getMimeType(format);
    output.data = result.data;
    return output;
}

QString PDFDocumentToolTableWriter::toXmlName(const QString& name)
{
    QString result;
    result.reserve(name.size() + 1);
    for (const QChar character : name)
    {
        const char16_t code = character.unicode();
        const bool isAllowed = (code >= u'a' && code <= u'z') || (code >= u'A' && code <= u'Z') || (code >= u'0' && code <= u'9') ||
                               code == u'-' || code == u'_' || code == u'.';
        result += isAllowed ? character : QLatin1Char('_');
    }

    if (result.isEmpty())
    {
        return QStringLiteral("item");
    }

    const QChar first = result.front();
    const bool isStartAllowed = (first >= QLatin1Char('a') && first <= QLatin1Char('z')) || (first >= QLatin1Char('A') && first <= QLatin1Char('Z')) || first == QLatin1Char('_');
    if (!isStartAllowed || result.startsWith(QLatin1String("xml"), Qt::CaseInsensitive))
    {
        result.prepend(QLatin1Char('_'));
    }
    return result;
}

QString PDFDocumentToolTableWriter::toXmlText(const QString& text)
{
    // XML 1.0, 2.2 Characters: #x9 | #xA | #xD | [#x20-#xD7FF] | [#xE000-#xFFFD] | [#x10000-#x10FFFF]
    QString result;
    result.reserve(text.size());
    const qsizetype size = text.size();
    for (qsizetype i = 0; i < size; ++i)
    {
        const QChar character = text.at(i);
        const char16_t code = character.unicode();
        if (character.isHighSurrogate() && i + 1 < size && text.at(i + 1).isLowSurrogate())
        {
            result += character;
            result += text.at(i + 1);
            ++i;
            continue;
        }

        const bool isControl = code < 0x20 && code != 0x09 && code != 0x0A && code != 0x0D;
        const bool isAllowed = !isControl && !character.isSurrogate() && code != 0xFFFE && code != 0xFFFF;
        result += isAllowed ? character : QChar(QChar::ReplacementCharacter);
    }
    return result;
}

QString PDFDocumentToolTableWriter::toCsvField(const QString& text, bool isDocumentText, QChar separator)
{
    QString field = text;

    // A text of the document, which a spreadsheet would take for a formula (OWASP CSV injection): the
    // apostrophe makes it a text again (B182). A number of the program is never escaped.
    if (isDocumentText && !field.isEmpty())
    {
        const QChar first = field.front();
        if (first == QLatin1Char('=') || first == QLatin1Char('+') || first == QLatin1Char('-') || first == QLatin1Char('@') ||
            first == QLatin1Char('\t') || first == QLatin1Char('\r'))
        {
            field.prepend(QLatin1Char('\''));
        }
    }

    // RFC 4180, 2.6 and 2.7: a field with a separator, a quote or a line break is quoted, its quotes doubled
    if (field.contains(separator) || field.contains(QLatin1Char('"')) || field.contains(QLatin1Char('\r')) || field.contains(QLatin1Char('\n')))
    {
        field.replace(QLatin1Char('"'), QLatin1String("\"\""));
        field = QLatin1Char('"') + field + QLatin1Char('"');
    }
    return field;
}

PDFDocumentToolTableFormat PDFDocumentToolTableWriter::getFormat(const QString& id)
{
    for (const PDFDocumentToolTableFormat format : { PDFDocumentToolTableFormat::Text, PDFDocumentToolTableFormat::Html, PDFDocumentToolTableFormat::Xml, PDFDocumentToolTableFormat::Csv })
    {
        if (id == getFormatId(format))
        {
            return format;
        }
    }
    return PDFDocumentToolTableFormat::None;
}

PDFDocumentToolTableFormat PDFDocumentToolTableWriter::getFormat(const QVariantMap& parameters)
{
    return getFormat(parameters.value(QLatin1String(PARAMETER_FORMAT)).toString());
}

QString PDFDocumentToolTableWriter::getFormatId(PDFDocumentToolTableFormat format)
{
    switch (format)
    {
        case PDFDocumentToolTableFormat::None:
            return QString::fromLatin1(FORMAT_NONE);
        case PDFDocumentToolTableFormat::Text:
            return QString::fromLatin1(FORMAT_TEXT);
        case PDFDocumentToolTableFormat::Html:
            return QString::fromLatin1(FORMAT_HTML);
        case PDFDocumentToolTableFormat::Xml:
            return QString::fromLatin1(FORMAT_XML);
        case PDFDocumentToolTableFormat::Csv:
            return QString::fromLatin1(FORMAT_CSV);
    }
    return QString::fromLatin1(FORMAT_NONE);
}

QString PDFDocumentToolTableWriter::getExtension(PDFDocumentToolTableFormat format)
{
    switch (format)
    {
        case PDFDocumentToolTableFormat::None:
            break;
        case PDFDocumentToolTableFormat::Text:
            return QStringLiteral("txt");
        case PDFDocumentToolTableFormat::Html:
            return QStringLiteral("html");
        case PDFDocumentToolTableFormat::Xml:
            return QStringLiteral("xml");
        case PDFDocumentToolTableFormat::Csv:
            return QStringLiteral("csv");
    }
    return QString();
}

QString PDFDocumentToolTableWriter::getMimeType(PDFDocumentToolTableFormat format)
{
    switch (format)
    {
        case PDFDocumentToolTableFormat::None:
            break;
        case PDFDocumentToolTableFormat::Text:
            return QStringLiteral("text/plain");
        case PDFDocumentToolTableFormat::Html:
            return QStringLiteral("text/html");
        case PDFDocumentToolTableFormat::Xml:
            return QStringLiteral("text/xml");
        case PDFDocumentToolTableFormat::Csv:
            return QStringLiteral("text/csv");
    }
    return QString();
}

PDFDocumentToolDateFormat PDFDocumentToolTableWriter::getDateFormat(const QString& id)
{
    if (id == QLatin1String(DATE_FORMAT_SHORT))
    {
        return PDFDocumentToolDateFormat::Short;
    }
    if (id == QLatin1String(DATE_FORMAT_LONG))
    {
        return PDFDocumentToolDateFormat::Long;
    }
    if (id == QLatin1String(DATE_FORMAT_RFC2822))
    {
        return PDFDocumentToolDateFormat::Rfc2822;
    }
    return PDFDocumentToolDateFormat::Iso;
}

PDFDocumentToolParameterDefinition PDFDocumentToolTableWriter::createFormatParameter(const std::vector<PDFDocumentToolTableFormat>& formats, PDFDocumentToolTableFormat defaultFormat)
{
    std::vector<PDFDocumentToolParameterOption> options;
    for (const PDFDocumentToolTableFormat format : formats)
    {
        switch (format)
        {
            case PDFDocumentToolTableFormat::None:
                options.push_back({ getFormatId(format), QT_TR_NOOP("No file"), QT_TR_NOOP("Only the data of the result, no file is written") });
                break;
            case PDFDocumentToolTableFormat::Text:
                options.push_back({ getFormatId(format), QT_TR_NOOP("Text"), QT_TR_NOOP("Plain text with aligned columns") });
                break;
            case PDFDocumentToolTableFormat::Html:
                options.push_back({ getFormatId(format), QT_TR_NOOP("HTML"), QT_TR_NOOP("A web page with tables") });
                break;
            case PDFDocumentToolTableFormat::Xml:
                options.push_back({ getFormatId(format), QT_TR_NOOP("XML"), QT_TR_NOOP("Structured data for other programs") });
                break;
            case PDFDocumentToolTableFormat::Csv:
                options.push_back({ getFormatId(format), QT_TR_NOOP("CSV"), QT_TR_NOOP("One table for a spreadsheet, separated by semicolons, UTF-8") });
                break;
        }
    }

    return PDFDocumentToolParameterDefinition::createEnum(tr, QString::fromLatin1(PARAMETER_FORMAT), QT_TR_NOOP("Format"), QT_TR_NOOP("Format of the exported file"), std::move(options), getFormatId(defaultFormat));
}

PDFDocumentToolParameterDefinition PDFDocumentToolTableWriter::createDateFormatParameter()
{
    std::vector<PDFDocumentToolParameterOption> options;
    options.push_back({ QString::fromLatin1(DATE_FORMAT_SHORT), QT_TR_NOOP("Short dates"), QT_TR_NOOP("The short format of the language") });
    options.push_back({ QString::fromLatin1(DATE_FORMAT_LONG), QT_TR_NOOP("Long dates"), QT_TR_NOOP("The long format of the language") });
    options.push_back({ QString::fromLatin1(DATE_FORMAT_ISO), QT_TR_NOOP("ISO 8601"), QT_TR_NOOP("Year-month-day with the offset from UTC") });
    options.push_back({ QString::fromLatin1(DATE_FORMAT_RFC2822), QT_TR_NOOP("RFC 2822"), QT_TR_NOOP("The format of e-mail messages") });
    return PDFDocumentToolParameterDefinition::createEnum(tr, QStringLiteral("dateFormat"), QT_TR_NOOP("Dates"), QT_TR_NOOP("How the dates of the report are written"), std::move(options), QString::fromLatin1(DATE_FORMAT_SHORT));
}

}   // namespace pdf
