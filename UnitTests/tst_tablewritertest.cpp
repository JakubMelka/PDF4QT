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

// The writer of the exports of the analysis tools (plan of step 7 of phase 2, 4.1, 8, R7-14; B182):
// CSV by RFC 4180 (fields, quotes, line breaks, UTF-8 without a BOM), the formula escape of the texts of a
// document (OWASP CSV injection), the numbers of CSV and XML always in the C locale while Text and HTML take
// the formatter, the escaping of HTML (a text is never markup), valid XML names and XML 1.0 characters.

#include "pdfdocumenttooltablewriter.h"

#include <QTimeZone>
#include <QXmlStreamReader>
#include <QtTest>

#include <cmath>
#include <limits>

using namespace pdf;

/// A formatter of another locale: the numbers and dates of Text and HTML must come from it, CSV and XML never
class TableWriterTestFormatter : public PDFDocumentToolValueFormatter
{
public:
    virtual QString formatInteger(qint64 value) const override { return QStringLiteral("<%1>").arg(value); }
    virtual QString formatReal(double value, int decimals) const override { return QString::number(value, 'f', decimals).replace(QLatin1Char('.'), QLatin1Char(',')); }
};

/// Helpers of the test
class TableWriterTestHelper
{
public:
    TableWriterTestHelper() = delete;

    /// A document with one table of the three kinds of the columns
    static PDFDocumentToolTableSection createDocument(const std::vector<std::vector<QVariant>>& rows)
    {
        PDFDocumentToolTable table;
        table.name = QStringLiteral("inks");
        table.title = QStringLiteral("Inks");
        table.addColumn(QStringLiteral("name"), QStringLiteral("Name"), PDFDocumentToolTableValueType::DocumentText);
        table.addColumn(QStringLiteral("count"), QStringLiteral("Count"), PDFDocumentToolTableValueType::Integer);
        table.addColumn(QStringLiteral("share"), QStringLiteral("Share (%)"), PDFDocumentToolTableValueType::Real, 2);
        for (const std::vector<QVariant>& row : rows)
        {
            table.addRow(row);
        }

        PDFDocumentToolTableSection document;
        document.name = QStringLiteral("statistics");
        document.title = QStringLiteral("Document Statistics");
        document.addTable(std::move(table));
        return document;
    }

    static QByteArray write(const PDFDocumentToolTableSection& document, PDFDocumentToolTableFormat format, const PDFDocumentToolValueFormatter* formatter = nullptr)
    {
        PDFDocumentToolTableWriter::Options options;
        options.formatter = formatter;
        const PDFDocumentToolTableWriter::Result result = PDFDocumentToolTableWriter::write(document, format, options);
        return result.isSuccess() ? result.data : QByteArray();
    }

    /// The texts of the elements of an XML document; empty and \p error, when it is not well-formed
    static QStringList readTexts(const QByteArray& xml, QString* error)
    {
        QStringList texts;
        QXmlStreamReader reader(xml);
        while (!reader.atEnd())
        {
            reader.readNext();
            if (reader.isCharacters() && !reader.isWhitespace())
            {
                texts << reader.text().toString();
            }
        }
        if (reader.hasError())
        {
            *error = reader.errorString();
            return QStringList();
        }
        return texts;
    }

    /// The names of the elements of an XML document
    static QStringList readElementNames(const QByteArray& xml)
    {
        QStringList names;
        QXmlStreamReader reader(xml);
        while (!reader.atEnd())
        {
            if (reader.readNext() == QXmlStreamReader::StartElement)
            {
                names << reader.name().toString();
            }
        }
        return reader.hasError() ? QStringList() : names;
    }
};

class TableWriterTest : public QObject
{
    Q_OBJECT

private slots:
    void csvRfc4180();
    void csvFormulaEscape();
    void csvAndXmlIgnoreFormatter();
    void csvOneTable();
    void csvSeparator();
    void htmlEscaping();
    void xmlNamesAndCharacters();
    void dates();
    void sectionDepth();
    void outputFile();
    void formatParameter();
};

void TableWriterTest::csvRfc4180()
{
    // RFC 4180, 2: CRLF after each record, the header first; 2.6: a field with a separator, a quote or a line
    // break is enclosed in quotes; 2.7: a quote inside is doubled. UTF-8 without a byte order mark.
    using H = TableWriterTestHelper;
    const PDFDocumentToolTableSection document = H::createDocument({ { QStringLiteral("Cyan"), qint64(1234567), 61.4199 },
                                                                     { QStringLiteral("PANTONE \"151\"; C"), qint64(0), 0.0 },
                                                                     { QStringLiteral("Line\nbreak"), QVariant(), QVariant() },
                                                                     { QString::fromUtf8("\xC5\xBDlu\xC5\xA5ou\xC4\x8Dk\xC3\xBD") } });
    const QByteArray csv = H::write(document, PDFDocumentToolTableFormat::Csv);
    const QByteArray expected = QByteArray("Name;Count;Share (%)\r\n") +
                                "Cyan;1234567;61.42\r\n" +
                                "\"PANTONE \"\"151\"\"; C\";0;0.00\r\n" +
                                "\"Line\nbreak\";;\r\n" +
                                "\xC5\xBDlu\xC5\xA5ou\xC4\x8Dk\xC3\xBD;;\r\n";
    QCOMPARE(csv, expected);
    QVERIFY(!csv.startsWith("\xEF\xBB\xBF"));
}

void TableWriterTest::csvFormulaEscape()
{
    // OWASP CSV injection (B182): a text of the document starting by = + - @ tab or CR is a formula for a
    // spreadsheet, it gets a leading apostrophe; a text of the program and a number are never changed
    using H = TableWriterTestHelper;
    const PDFDocumentToolTableSection document = H::createDocument({ { QStringLiteral("=HYPERLINK(\"http://x\")"), qint64(-5), -1.5 },
                                                                     { QStringLiteral("+1"), qint64(1), 1.0 },
                                                                     { QStringLiteral("-2"), qint64(1), 1.0 },
                                                                     { QStringLiteral("@SUM(A1)"), qint64(1), 1.0 },
                                                                     { QStringLiteral("\tTab"), qint64(1), 1.0 },
                                                                     { QStringLiteral("a=b"), qint64(1), 1.0 } });
    const QList<QByteArray> lines = H::write(document, PDFDocumentToolTableFormat::Csv).split('\n');
    QCOMPARE(lines.value(1), QByteArray("\"'=HYPERLINK(\"\"http://x\"\")\";-5;-1.50\r"));
    QCOMPARE(lines.value(2), QByteArray("'+1;1;1.00\r"));
    QCOMPARE(lines.value(3), QByteArray("'-2;1;1.00\r"));
    QCOMPARE(lines.value(4), QByteArray("'@SUM(A1);1;1.00\r"));
    QCOMPARE(lines.value(5), QByteArray("'\tTab;1;1.00\r"));
    QCOMPARE(lines.value(6), QByteArray("a=b;1;1.00\r"));

    // A text of the program is never escaped
    QCOMPARE(PDFDocumentToolTableWriter::toCsvField(QStringLiteral("=Total"), false, QLatin1Char(';')), QStringLiteral("=Total"));
    QCOMPARE(PDFDocumentToolTableWriter::toCsvField(QStringLiteral("=Total"), true, QLatin1Char(';')), QStringLiteral("'=Total"));
}

void TableWriterTest::csvAndXmlIgnoreFormatter()
{
    // R7-14: Text and HTML are for reading (the formatter of the application), CSV and XML are data in the C
    // locale - the same file whatever the language of the user interface
    using H = TableWriterTestHelper;
    const TableWriterTestFormatter formatter;
    const PDFDocumentToolTableSection document = H::createDocument({ { QStringLiteral("Black"), qint64(42), 12.5 } });

    const QByteArray csv = H::write(document, PDFDocumentToolTableFormat::Csv, &formatter);
    QVERIFY2(csv.contains("Black;42;12.50\r\n"), csv.constData());

    QString error;
    const QStringList xmlTexts = H::readTexts(H::write(document, PDFDocumentToolTableFormat::Xml, &formatter), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(xmlTexts.contains(QStringLiteral("42")));
    QVERIFY(xmlTexts.contains(QStringLiteral("12.50")));

    const QString text = QString::fromUtf8(H::write(document, PDFDocumentToolTableFormat::Text, &formatter));
    QVERIFY2(text.contains(QStringLiteral("<42>")) && text.contains(QStringLiteral("12,50")), qPrintable(text));
    QVERIFY(!text.contains(QStringLiteral("12.50")));

    const QStringList htmlTexts = H::readTexts(H::write(document, PDFDocumentToolTableFormat::Html, &formatter), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(htmlTexts.contains(QStringLiteral("<42>")));
    QVERIFY(htmlTexts.contains(QStringLiteral("12,50")));

    // Without a formatter the C locale: no group separator, a dot; a non-finite number is an empty cell
    const QString plain = QString::fromUtf8(H::write(H::createDocument({ { QStringLiteral("K"), qint64(1234567), 0.126 } }), PDFDocumentToolTableFormat::Text));
    QVERIFY2(plain.contains(QStringLiteral("1234567")) && plain.contains(QStringLiteral("0.13")), qPrintable(plain));
    const QByteArray nan = H::write(H::createDocument({ { QStringLiteral("K"), qint64(1), std::numeric_limits<double>::quiet_NaN() } }), PDFDocumentToolTableFormat::Csv);
    QVERIFY2(nan.endsWith("K;1;\r\n"), nan.constData());
}

void TableWriterTest::csvOneTable()
{
    // K7.20: a CSV file is one table (the current view); a document of none or of two tables is refused
    using H = TableWriterTestHelper;
    PDFDocumentToolTableWriter::Options options;
    PDFDocumentToolTableSection empty;
    empty.addParagraph(QStringLiteral("No table"));
    QVERIFY(!PDFDocumentToolTableWriter::write(empty, PDFDocumentToolTableFormat::Csv, options).isSuccess());

    PDFDocumentToolTableSection two = H::createDocument({ { QStringLiteral("A"), qint64(1), 1.0 } });
    PDFDocumentToolTableSection* nested = two.addSection(QStringLiteral("types"), QStringLiteral("Types"));
    nested->addTable(H::createDocument({ { QStringLiteral("B"), qint64(2), 2.0 } }).items.front().table);
    QCOMPARE(two.getTableCount(), 2);
    QVERIFY(!PDFDocumentToolTableWriter::write(two, PDFDocumentToolTableFormat::Csv, options).isSuccess());

    // One table in a nested section is the table of the file; the other formats write both tables
    PDFDocumentToolTableSection one;
    one.addSection(QStringLiteral("section"), QStringLiteral("Section"))->addTable(H::createDocument({ { QStringLiteral("C"), qint64(3), 3.0 } }).items.front().table);
    QCOMPARE(H::write(one, PDFDocumentToolTableFormat::Csv), QByteArray("Name;Count;Share (%)\r\nC;3;3.00\r\n"));
    const QString text = QString::fromUtf8(H::write(two, PDFDocumentToolTableFormat::Text));
    QVERIFY(text.contains(QStringLiteral(" A ")) && text.contains(QStringLiteral(" B ")) && text.contains(QStringLiteral("Types")));

    // None is no file
    QVERIFY(!PDFDocumentToolTableWriter::write(one, PDFDocumentToolTableFormat::None, options).isSuccess());
}

void TableWriterTest::csvSeparator()
{
    // The separator is a choice (plan 4.1); a quote or a line break can never be one (RFC 4180, 2.6)
    using H = TableWriterTestHelper;
    const PDFDocumentToolTableSection document = H::createDocument({ { QStringLiteral("a,b;c"), qint64(1), 1.0 } });
    PDFDocumentToolTableWriter::Options options;
    options.csvSeparator = QLatin1Char(',');
    QCOMPARE(PDFDocumentToolTableWriter::write(document, PDFDocumentToolTableFormat::Csv, options).data, QByteArray("Name,Count,Share (%)\r\n\"a,b;c\",1,1.00\r\n"));

    options.csvSeparator = QLatin1Char('"');
    QVERIFY(!PDFDocumentToolTableWriter::write(document, PDFDocumentToolTableFormat::Csv, options).isSuccess());
    options.csvSeparator = QLatin1Char('\n');
    QVERIFY(!PDFDocumentToolTableWriter::write(document, PDFDocumentToolTableFormat::Csv, options).isSuccess());
}

void TableWriterTest::htmlEscaping()
{
    // Plan 6.2: a text of the document is never markup of the report - "</script>" stays a text; a code
    // keeps its lines and spaces (<pre>)
    using H = TableWriterTestHelper;
    const QString script = QStringLiteral("</script><script>alert(1)</script>");
    const QString code = QStringLiteral("function f() {\n    app.alert(\"<b>\");\n}");
    PDFDocumentToolTableSection document = H::createDocument({ { QStringLiteral("<b>&amp;</b>"), qint64(1), 1.0 } });
    PDFDocumentToolTableSection* section = document.addSection(QStringLiteral("javascript"), QStringLiteral("JavaScript"));
    section->addParagraph(script);
    section->addCode(code);

    const QByteArray html = H::write(document, PDFDocumentToolTableFormat::Html);
    QVERIFY(!html.contains("<script>"));
    QVERIFY(!html.contains("<b>"));
    QVERIFY(html.contains("<pre>"));
    QVERIFY(html.contains("charset=UTF-8"));

    QString error;
    const QStringList texts = H::readTexts(html, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(texts.contains(script));
    QVERIFY(texts.contains(code));
    QVERIFY(texts.contains(QStringLiteral("<b>&amp;</b>")));
}

void TableWriterTest::xmlNamesAndCharacters()
{
    // XML 1.0: a name starts by a letter or '_', the names starting by "xml" are reserved (2.3); the
    // characters of 2.2 only - a control character or a lone surrogate of a document is replaced, so the file
    // is always well-formed (B179: the names of the elements are never the names of the inks)
    QCOMPARE(PDFDocumentToolTableWriter::toXmlName(QStringLiteral("count")), QStringLiteral("count"));
    QCOMPARE(PDFDocumentToolTableWriter::toXmlName(QStringLiteral("1st col")), QStringLiteral("_1st_col"));
    QCOMPARE(PDFDocumentToolTableWriter::toXmlName(QStringLiteral("XmlData")), QStringLiteral("_XmlData"));
    QCOMPARE(PDFDocumentToolTableWriter::toXmlName(QStringLiteral("PANTONE 151 C")), QStringLiteral("PANTONE_151_C"));
    QCOMPARE(PDFDocumentToolTableWriter::toXmlName(QString()), QStringLiteral("item"));
    QCOMPARE(PDFDocumentToolTableWriter::toXmlName(QStringLiteral("-a")), QStringLiteral("_-a"));

    const QString hostile = QStringLiteral("a") + QChar(0x0001) + QStringLiteral("b") + QChar(0xD800) + QStringLiteral("c") + QChar(0xFFFE);
    const QString replaced = QStringLiteral("a") + QChar(QChar::ReplacementCharacter) + QStringLiteral("b") + QChar(QChar::ReplacementCharacter) + QStringLiteral("c") + QChar(QChar::ReplacementCharacter);
    QCOMPARE(PDFDocumentToolTableWriter::toXmlText(hostile), replaced);
    const QString pair = QString::fromUtf8("\xF0\x9F\x98\x80");     // U+1F600, a valid surrogate pair
    QCOMPARE(PDFDocumentToolTableWriter::toXmlText(pair), pair);

    using H = TableWriterTestHelper;
    PDFDocumentToolTable table;
    table.name = QStringLiteral("1 table");
    table.addColumn(QStringLiteral("ink name"), QStringLiteral("Ink"), PDFDocumentToolTableValueType::DocumentText);
    table.addRow({ hostile });
    PDFDocumentToolTableSection document;
    document.name = QStringLiteral("report");
    document.addTable(std::move(table));

    for (const PDFDocumentToolTableFormat format : { PDFDocumentToolTableFormat::Xml, PDFDocumentToolTableFormat::Html })
    {
        QString error;
        const QStringList texts = H::readTexts(H::write(document, format), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(texts.contains(replaced));
    }
    const QStringList names = H::readElementNames(H::write(document, PDFDocumentToolTableFormat::Xml));
    QVERIFY2(names.contains(QStringLiteral("_1_table")) && names.contains(QStringLiteral("ink_name")), qPrintable(names.join(QLatin1Char(' '))));
}

void TableWriterTest::dates()
{
    // ISO 8601 with the offset from UTC in CSV and XML; RFC 2822 reads back to the same moment
    const QDateTime dateTime(QDate(2026, 11, 17), QTime(14, 5, 0), QTimeZone::fromSecondsAheadOfUtc(3600));
    const PDFDocumentToolValueFormatter* formatter = PDFDocumentToolValueFormatter::getCLocale();
    QCOMPARE(formatter->formatDateTime(dateTime, PDFDocumentToolDateFormat::Iso), QStringLiteral("2026-11-17T14:05:00+01:00"));
    QCOMPARE(PDFDocumentToolValueFormatter::formatData(dateTime, PDFDocumentToolTableValueType::DateTime, 0), QStringLiteral("2026-11-17T14:05:00+01:00"));
    const QString rfc = formatter->formatDateTime(dateTime, PDFDocumentToolDateFormat::Rfc2822);
    QCOMPARE(QDateTime::fromString(rfc, Qt::RFC2822Date).toUTC(), dateTime.toUTC());
    QVERIFY(formatter->formatDateTime(QDateTime(), PDFDocumentToolDateFormat::Iso).isEmpty());

    PDFDocumentToolTable table;
    table.addColumn(QStringLiteral("created"), QStringLiteral("Created"), PDFDocumentToolTableValueType::DateTime);
    table.addRow({ dateTime });
    PDFDocumentToolTableSection document;
    document.addTable(std::move(table));
    QCOMPARE(TableWriterTestHelper::write(document, PDFDocumentToolTableFormat::Csv), QByteArray("Created\r\n2026-11-17T14:05:00+01:00\r\n"));
}

void TableWriterTest::sectionDepth()
{
    // A model nested deeper than the limit is refused, never written by a recursion without an end
    PDFDocumentToolTableSection document;
    PDFDocumentToolTableSection* section = &document;
    for (int i = 0; i <= PDFDocumentToolTableWriter::MAXIMUM_SECTION_DEPTH; ++i)
    {
        section = section->addSection(QStringLiteral("s%1").arg(i), QStringLiteral("Section %1").arg(i));
    }
    PDFDocumentToolTableWriter::Options options;
    QVERIFY(!PDFDocumentToolTableWriter::write(document, PDFDocumentToolTableFormat::Html, options).isSuccess());

    // At the limit it is written
    PDFDocumentToolTableSection limited;
    section = &limited;
    for (int i = 0; i < PDFDocumentToolTableWriter::MAXIMUM_SECTION_DEPTH; ++i)
    {
        section = section->addSection(QStringLiteral("s%1").arg(i), QStringLiteral("Section %1").arg(i));
    }
    section->addParagraph(QStringLiteral("Deepest"));
    const PDFDocumentToolTableWriter::Result result = PDFDocumentToolTableWriter::write(limited, PDFDocumentToolTableFormat::Xml, options);
    QVERIFY(result.isSuccess());
    QString error;
    QVERIFY(TableWriterTestHelper::readTexts(result.data, &error).contains(QStringLiteral("Deepest")));
}

void TableWriterTest::outputFile()
{
    // The output of a result: the name with the extension of the format and its type
    const PDFDocumentToolTableSection document = TableWriterTestHelper::createDocument({ { QStringLiteral("A"), qint64(1), 1.0 } });
    PDFDocumentToolTableWriter::Options options;
    QString error;
    const PDFDocumentToolOutputFile csv = PDFDocumentToolTableWriter::createOutput(document, PDFDocumentToolTableFormat::Csv, options, QStringLiteral("Report - statistics"), &error);
    QCOMPARE(csv.suggestedFileName, QStringLiteral("Report - statistics.csv"));
    QCOMPARE(csv.mimeType, QStringLiteral("text/csv"));
    QCOMPARE(csv.data, QByteArray("Name;Count;Share (%)\r\nA;1;1.00\r\n"));

    const PDFDocumentToolOutputFile html = PDFDocumentToolTableWriter::createOutput(document, PDFDocumentToolTableFormat::Html, options, QStringLiteral("Report"), &error);
    QCOMPARE(html.suggestedFileName, QStringLiteral("Report.html"));
    QCOMPARE(html.mimeType, QStringLiteral("text/html"));

    const PDFDocumentToolOutputFile none = PDFDocumentToolTableWriter::createOutput(document, PDFDocumentToolTableFormat::None, options, QStringLiteral("Report"), &error);
    QVERIFY(none.suggestedFileName.isEmpty());
    QVERIFY(!error.isEmpty());
}

void TableWriterTest::formatParameter()
{
    // The parameter "format" of the analysis tools: the offered formats in their order, the default
    const PDFDocumentToolParameterDefinition definition = PDFDocumentToolTableWriter::createFormatParameter({ PDFDocumentToolTableFormat::None, PDFDocumentToolTableFormat::Csv,
                                                                                                             PDFDocumentToolTableFormat::Text }, PDFDocumentToolTableFormat::None);
    QCOMPARE(definition.getName(), QStringLiteral("format"));
    QStringList ids;
    for (const PDFDocumentToolParameterOption& option : definition.getOptions())
    {
        ids << option.id;
    }
    QCOMPARE(ids, QStringList({ QStringLiteral("none"), QStringLiteral("csv"), QStringLiteral("text") }));
    QCOMPARE(definition.getDefaultValue().toString(), QStringLiteral("none"));

    for (const PDFDocumentToolTableFormat format : { PDFDocumentToolTableFormat::None, PDFDocumentToolTableFormat::Text, PDFDocumentToolTableFormat::Html,
                                                     PDFDocumentToolTableFormat::Xml, PDFDocumentToolTableFormat::Csv })
    {
        QCOMPARE(PDFDocumentToolTableWriter::getFormat(PDFDocumentToolTableWriter::getFormatId(format)), format);
    }
    QCOMPARE(PDFDocumentToolTableWriter::getFormat(QStringLiteral("CSV")), PDFDocumentToolTableFormat::None);
    QCOMPARE(PDFDocumentToolTableWriter::getDateFormat(QStringLiteral("rfc2822")), PDFDocumentToolDateFormat::Rfc2822);
    QCOMPARE(PDFDocumentToolTableWriter::getDateFormat(QStringLiteral("unknown")), PDFDocumentToolDateFormat::Iso);
}

QTEST_GUILESS_MAIN(TableWriterTest)

#include "tst_tablewritertest.moc"
