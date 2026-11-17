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

#ifndef PDFDOCUMENTTOOLTABLEWRITER_H
#define PDFDOCUMENTTOOLTABLEWRITER_H

#include "pdfglobal.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttoolparameter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QVariant>

#include <vector>

namespace pdf
{

// The one writer of the exports of the analysis tools of step 7 of phase 2 (plan
// GUI_overhaul/newapp_tools_step7.md 4.1, 9.3, R7-14): Document Statistics, Ink Coverage, Document
// Report, Measure, Compare and Output Preview write their tables and sections by it, the application,
// PdfTool and MCP alike. A tool builds the model below from the data of its result (its static
// writeOutput) - the export never runs the tool again.

/// File format of an export of an analysis tool (the parameter "format" of the tools; None = no file,
/// the data only - MCP and a run of the GUI, whose export comes later)
enum class PDFDocumentToolTableFormat
{
    None,   ///< No file ("none")
    Text,   ///< Plain text with aligned columns ("text", .txt)
    Html,   ///< XHTML ("html", .html)
    Xml,    ///< XML of the namespace of PDF4QT ("xml", .xml)
    Csv     ///< One table by RFC 4180 ("csv", .csv)
};

/// How a date and a time are written (Document Report, the parameter "dateFormat": 497:20544)
enum class PDFDocumentToolDateFormat
{
    Short,      ///< The short format of the locale ("short")
    Long,       ///< The long format of the locale ("long")
    Iso,        ///< ISO 8601 with the offset from UTC ("iso"), never translated
    Rfc2822     ///< RFC 2822 ("rfc2822"), never translated
};

/// Kind of the values of a column (how a cell is written)
enum class PDFDocumentToolTableValueType
{
    Text,           ///< A text of the program (a name of a class, a translated label)
    DocumentText,   ///< A text taken from the document (a name of a font or an ink, a title): in CSV a leading = + - @ tab or CR is escaped by an apostrophe (formula injection, B182)
    Integer,        ///< A whole number (qint64)
    Real,           ///< A real number with PDFDocumentToolTableColumn::decimals places
    DateTime        ///< A date and a time (QDateTime) by the date format of the options
};

/// Formatter of the numbers and dates of the files for reading (Text, HTML), R7-14: the core has no
/// locale of the application. The base class writes the C locale (PdfTool, MCP, the tests): digits
/// without a group separator, a dot, the C locale for the short and the long date. The application
/// derives PDFAppToolValueFormatter (PDFAppLocale, O17). CSV and XML never use it: they are data and
/// are written in the C locale with a dot and the dates by ISO 8601 (always the same file for the same
/// data). Reentrant, used by one writer at a time; a non-finite real is an empty text.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolValueFormatter
{
public:
    PDFDocumentToolValueFormatter() = default;
    virtual ~PDFDocumentToolValueFormatter();

    PDFDocumentToolValueFormatter(const PDFDocumentToolValueFormatter&) = delete;
    PDFDocumentToolValueFormatter& operator=(const PDFDocumentToolValueFormatter&) = delete;

    /// A whole number ("1234567" in the C locale)
    virtual QString formatInteger(qint64 value) const;

    /// A real number with \p decimals places ("61.42"); NaN and infinity give an empty text
    virtual QString formatReal(double value, int decimals) const;

    /// A date and a time by \p format; an invalid value gives an empty text
    virtual QString formatDateTime(const QDateTime& dateTime, PDFDocumentToolDateFormat format) const;

    /// The formatter of the C locale (a static instance, never deleted)
    static const PDFDocumentToolValueFormatter* getCLocale();

    /// The text of a value in the C locale (CSV, XML; ISO 8601 for a date with the offset)
    static QString formatData(const QVariant& value, PDFDocumentToolTableValueType type, int decimals);
};

/// A column of a table: its name (the XML element, sanitized by PDFDocumentToolTableWriter::toXmlName),
/// its translated title (the header row) and the kind of its values
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolTableColumn
{
public:
    QString name;                                                       ///< Name of the column ("count"), the XML element
    QString title;                                                      ///< Translated title ("Count")
    PDFDocumentToolTableValueType type = PDFDocumentToolTableValueType::Text;   ///< Kind of the values
    int decimals = 2;                                                   ///< Places of a Real column (0 to 10)
};

/// A table of an export: columns and rows of cells. A cell is a QVariant of the type of its column
/// (QString, qint64, double, QDateTime); an invalid QVariant is an empty cell; a row shorter than the
/// columns has empty cells at its end, the cells beyond the columns are not written.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolTable
{
public:
    QString name;                                       ///< Name of the table ("byFunction"), the XML element
    QString title;                                      ///< Translated title ("Statistics by object function")
    std::vector<PDFDocumentToolTableColumn> columns;    ///< Columns
    std::vector<std::vector<QVariant>> rows;            ///< Rows of the cells

    /// Adds a column
    void addColumn(QString columnName, QString columnTitle, PDFDocumentToolTableValueType type, int decimals = 2);

    /// Adds a row
    void addRow(std::vector<QVariant> cells) { rows.push_back(std::move(cells)); }
};

/// An item of a section: a paragraph, a block of code (a JavaScript of the report: its lines and spaces
/// kept, never run), a table or a nested section
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolTableItem
{
public:
    enum class Kind
    {
        Paragraph,
        Code,
        Table,
        Section
    };

    Kind kind = Kind::Paragraph;
    QString text;                   ///< Paragraph, Code: the text
    PDFDocumentToolTable table;     ///< Table: the table
    int sectionIndex = -1;          ///< Section: the index of the nested section in PDFDocumentToolTableSection::sections
};

/// A section of an export (a heading with its items); the document is its root section. Nested
/// sections deeper than PDFDocumentToolTableWriter::MAXIMUM_SECTION_DEPTH are not written (the writer
/// says so by an error).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolTableSection
{
public:
    QString name;                                   ///< Name ("statistics"), the XML element
    QString title;                                  ///< Translated title ("Document Statistics")
    std::vector<PDFDocumentToolTableItem> items;    ///< Items in their order
    std::vector<PDFDocumentToolTableSection> sections;  ///< The nested sections, in the order of their items

    void addParagraph(QString paragraph);
    void addCode(QString code);
    void addTable(PDFDocumentToolTable table);

    /// Adds a nested section and returns it (valid until the next item is added)
    PDFDocumentToolTableSection* addSection(QString sectionName, QString sectionTitle);

    /// Number of the tables of the section and of its nested sections (up to the maximal depth)
    int getTableCount() const;
};

/// Writer of the exports of the analysis tools (plan of step 7 of phase 2, 4.1, 9.3, R7-14):
///  - Text: the titles, the paragraphs, the code with its lines and the tables with aligned columns
///    (numbers to the right), PDFOutputFormatter of the style Text; numbers and dates by the formatter
///    of the options;
///  - HTML: XHTML of PDFOutputFormatter (every text escaped by QXmlStreamWriter: a text of a document is
///    never markup - "</script>" stays a text), a code is <pre>; numbers and dates by the formatter;
///  - XML: the elements of the names of the sections, tables and columns (sanitized to a valid XML
///    name: toXmlName) in the namespace of PDF4QT, the values in the C locale and ISO 8601;
///  - CSV: exactly one table of the document (CSV is one table, K7.20: the tool gives the current
///    view), RFC 4180 - a header row of the titles, the separator of the options (';' by default),
///    a field with the separator, a quote, CR or LF quoted with its quotes doubled, CRLF after each
///    row, UTF-8 without a BOM; numbers in the C locale with a dot, dates by ISO 8601; a text of the
///    document starting by = + - @ tab or CR gets a leading apostrophe (formula injection, B182).
/// Every text of HTML and XML has the characters not allowed in XML 1.0 (controls other than tab,
/// CR, LF; U+FFFE, U+FFFF; a lone surrogate) replaced by U+FFFD, so the file is always well-formed.
/// The output is UTF-8. Reentrant: only its arguments.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolTableWriter
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolTableWriter)

public:
    PDFDocumentToolTableWriter() = delete;

    /// Deepest nesting of the sections, which is written
    static constexpr int MAXIMUM_SECTION_DEPTH = 32;

    /// Name of the parameter of the format of the analysis tools ("format")
    static constexpr const char* PARAMETER_FORMAT = "format";

    /// Identifiers of the formats (the values of the parameter "format")
    static constexpr const char* FORMAT_NONE = "none";
    static constexpr const char* FORMAT_TEXT = "text";
    static constexpr const char* FORMAT_HTML = "html";
    static constexpr const char* FORMAT_XML = "xml";
    static constexpr const char* FORMAT_CSV = "csv";

    /// Identifiers of the date formats (the values of the parameter "dateFormat" of the report)
    static constexpr const char* DATE_FORMAT_SHORT = "short";
    static constexpr const char* DATE_FORMAT_LONG = "long";
    static constexpr const char* DATE_FORMAT_ISO = "iso";
    static constexpr const char* DATE_FORMAT_RFC2822 = "rfc2822";

    /// What the writer writes besides the model
    class Options
    {
    public:
        const PDFDocumentToolValueFormatter* formatter = nullptr;               ///< Numbers and dates of Text and HTML (nullptr = the C locale); owned by the caller for the time of the call
        PDFDocumentToolDateFormat dateFormat = PDFDocumentToolDateFormat::Iso;  ///< Dates of Text and HTML
        QChar csvSeparator = QLatin1Char(';');                                  ///< Separator of CSV (';' of the frames, ',' or a tab; another character than a quote, CR or LF)
    };

    /// The result of a write
    class Result
    {
    public:
        QByteArray data;        ///< The bytes of the file (UTF-8)
        QString errorMessage;   ///< Translated reason of a failure (empty = success)

        bool isSuccess() const { return errorMessage.isEmpty(); }
    };

    /// Writes the document (its root section) in the format. None and an invalid model (CSV of no or of
    /// more tables, a section too deep) give an error.
    /// \param document The root section (its title is the title of the file)
    /// \param format The format
    /// \param options The formatter, the date format, the separator of CSV
    static Result write(const PDFDocumentToolTableSection& document, PDFDocumentToolTableFormat format, const Options& options);

    /// Writes one table as CSV (RFC 4180, see the class)
    /// \param table The table
    /// \param separator Separator of the fields
    static QByteArray writeCsv(const PDFDocumentToolTable& table, QChar separator);

    /// An output file of the result (PDFDocumentToolResult::outputs) of the document: "<baseName>.<ext>",
    /// its type ("text/plain", "text/html", "text/xml", "text/csv") and its bytes. An empty
    /// suggestedFileName and the reason in \p errorMessage on a failure.
    /// \param document The root section
    /// \param format The format (not None)
    /// \param options The options of the write
    /// \param baseName Suggested name without the extension ("Project_Report - statistics")
    /// \param errorMessage Reason of a failure (optional)
    static PDFDocumentToolOutputFile createOutput(const PDFDocumentToolTableSection& document, PDFDocumentToolTableFormat format, const Options& options, const QString& baseName, QString* errorMessage);

    /// A valid XML name of \p name: the ASCII letters, digits, '-', '_' and '.' are kept, any other
    /// character becomes '_'; a name, which does not start by a letter or '_', gets a leading '_'; an
    /// empty name is "item"; a name starting by "xml" in any case gets a leading '_' (reserved)
    static QString toXmlName(const QString& name);

    /// The text with the characters not allowed in XML 1.0 replaced by U+FFFD
    static QString toXmlText(const QString& text);

    /// The text of a field of CSV: the formula escape of a text of the document, the quotes of RFC 4180
    /// \param text The text of the cell
    /// \param isDocumentText The text comes from the document (PDFDocumentToolTableValueType::DocumentText)
    /// \param separator Separator of the fields
    static QString toCsvField(const QString& text, bool isDocumentText, QChar separator);

    /// Format by its identifier ("csv"), None for an unknown one
    static PDFDocumentToolTableFormat getFormat(const QString& id);

    /// Format of the normalized parameters of a tool (the parameter "format")
    static PDFDocumentToolTableFormat getFormat(const QVariantMap& parameters);

    /// Identifier of a format ("csv")
    static QString getFormatId(PDFDocumentToolTableFormat format);

    /// Extension of the file of a format without the dot ("csv"; empty for None)
    static QString getExtension(PDFDocumentToolTableFormat format);

    /// Type of the file of a format ("text/csv"; empty for None)
    static QString getMimeType(PDFDocumentToolTableFormat format);

    /// Date format by its identifier ("iso"), Iso for an unknown one
    static PDFDocumentToolDateFormat getDateFormat(const QString& id);

    /// The parameter "format" of an analysis tool: an Enum of \p formats in their order (None first,
    /// when it is offered), the default \p defaultFormat. The texts are of this class.
    /// \param formats The offered formats
    /// \param defaultFormat The default (one of \p formats)
    static PDFDocumentToolParameterDefinition createFormatParameter(const std::vector<PDFDocumentToolTableFormat>& formats, PDFDocumentToolTableFormat defaultFormat);

    /// The parameter "dateFormat" of Document Report (short, long, iso, rfc2822; default short)
    static PDFDocumentToolParameterDefinition createDateFormatParameter();
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOLTABLEWRITER_H
