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

#include <QtTest>

#include "pdfformfieldformat.h"
#include "pdfaction.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"

using pdf::PDFFormFieldFormat;
using Function = pdf::PDFFormFieldFormat::Function;

class FormFieldFormatTest : public QObject
{
    Q_OBJECT

private slots:
    void test_parse_data();
    void test_parse();
    void test_number_format_data();
    void test_number_format();
    void test_number_keystroke();
    void test_percent();
    void test_date_print();
    void test_date_format_data();
    void test_date_format();
    void test_date_keystroke();
    void test_special_format_data();
    void test_special_format();
    void test_special_keystroke();
    void test_print_mask();
    void test_unsupported_function();
    void test_string_escapes();
    void test_make_number();
    void test_action();
    void test_scan_date();
    void test_guess_date();

private:
    /// Returns committed text, or "<rejected>", if text is rejected
    static QString commitText(const PDFFormFieldFormat& format, const QString& text);
};

QString FormFieldFormatTest::commitText(const PDFFormFieldFormat& format, const QString& text)
{
    return format.commit(text).value_or(QStringLiteral("<rejected>"));
}

void FormFieldFormatTest::test_parse_data()
{
    QTest::addColumn<QString>("script");
    QTest::addColumn<int>("function");

    QTest::newRow("number format") << QStringLiteral("AFNumber_Format(2, 3, 0, 0, \"\", false);") << int(Function::NumberFormat);
    QTest::newRow("number keystroke") << QStringLiteral("AFNumber_Keystroke(2, 3, 0, 0, \"\", false);") << int(Function::NumberKeystroke);
    QTest::newRow("whitespace, no semicolon") << QStringLiteral("\r\n  AFPercent_Format( 1 , 0 )  \r\n") << int(Function::PercentFormat);
    QTest::newRow("comments") << QStringLiteral("/* generated */ AFPercent_Keystroke(1, 0); // percent\r\n") << int(Function::PercentKeystroke);
    QTest::newRow("date format ex") << QStringLiteral("AFDate_FormatEx(\"dd.mm.yyyy\");") << int(Function::DateFormat);
    QTest::newRow("date keystroke index") << QStringLiteral("AFDate_Keystroke(2);") << int(Function::DateKeystroke);
    QTest::newRow("time format index") << QStringLiteral("AFTime_Format(1);") << int(Function::DateFormat);
    QTest::newRow("time keystroke ex") << QStringLiteral("AFTime_KeystrokeEx('HH:MM');") << int(Function::DateKeystroke);
    QTest::newRow("special format") << QStringLiteral("AFSpecial_Format(3);") << int(Function::SpecialFormat);
    QTest::newRow("special keystroke") << QStringLiteral("AFSpecial_Keystroke(0);") << int(Function::SpecialKeystroke);
    QTest::newRow("special keystroke mask") << QStringLiteral("AFSpecial_KeystrokeEx(\"99999-9999\");") << int(Function::SpecialKeystrokeMask);
    QTest::newRow("additional code") << QStringLiteral("AFNumber_Format(2, 3, 0, 0, \"\", false); calculate();") << int(Function::None);
    QTest::newRow("variable argument") << QStringLiteral("AFNumber_Format(nDec, 3, 0, 0, \"\", false);") << int(Function::None);
    QTest::newRow("custom script") << QStringLiteral("event.value = util.printf(\"%.2f\", event.value);") << int(Function::None);
    QTest::newRow("unknown function") << QStringLiteral("AFRange_Validate(true, 0, true, 100);") << int(Function::None);
    QTest::newRow("date index out of range") << QStringLiteral("AFDate_Format(14);") << int(Function::None);
    QTest::newRow("special index out of range") << QStringLiteral("AFSpecial_Format(4);") << int(Function::None);
    QTest::newRow("unterminated string") << QStringLiteral("AFDate_FormatEx(\"dd.mm.yyyy);") << int(Function::None);
    QTest::newRow("unterminated comment") << QStringLiteral("/* AFDate_FormatEx(\"dd.mm.yyyy\");") << int(Function::None);
    QTest::newRow("empty") << QString() << int(Function::None);
    QTest::newRow("identifier with dollar") << QStringLiteral("$AF();") << int(Function::None);
    QTest::newRow("identifier with digit") << QStringLiteral("AF1_Format();") << int(Function::None);
    QTest::newRow("identifier starting with digit") << QStringLiteral("1AF();") << int(Function::None);
    QTest::newRow("identifier only") << QStringLiteral("AFSpecial_Format") << int(Function::None);
    QTest::newRow("missing arguments end") << QStringLiteral("AFSpecial_Format(") << int(Function::None);
    QTest::newRow("unterminated arguments") << QStringLiteral("AFSpecial_Format(1") << int(Function::None);
    QTest::newRow("missing separator") << QStringLiteral("AFSpecial_Format(1 2)") << int(Function::None);
    QTest::newRow("invalid character") << QStringLiteral("AFSpecial_Format(#)") << int(Function::None);
    QTest::newRow("expression") << QStringLiteral("AFSpecial_Format(4/2);") << int(Function::None);
    QTest::newRow("text after call") << QStringLiteral("AFSpecial_Format(0) x") << int(Function::None);
    QTest::newRow("identifier argument at end") << QStringLiteral("AFSpecial_Format(true") << int(Function::None);
    QTest::newRow("line comment at end") << QStringLiteral("AFSpecial_Format(0); // comment") << int(Function::SpecialFormat);
    QTest::newRow("line comment before call") << QStringLiteral("// comment\nAFSpecial_Format(0);") << int(Function::SpecialFormat);
    QTest::newRow("backslash at end of script") << QStringLiteral("AFSpecial_KeystrokeEx(\"99\\") << int(Function::None);
    QTest::newRow("line feed in string") << QStringLiteral("AFSpecial_KeystrokeEx(\"9\n9\");") << int(Function::None);
    QTest::newRow("carriage return in string") << QStringLiteral("AFSpecial_KeystrokeEx(\"9\r9\");") << int(Function::None);
    QTest::newRow("truncated unicode escape") << QStringLiteral("AFSpecial_KeystrokeEx(\"\\u12") << int(Function::None);
    QTest::newRow("invalid unicode escape") << QStringLiteral("AFSpecial_KeystrokeEx(\"\\uZZZZ\");") << int(Function::None);
    QTest::newRow("line continuation at end") << QStringLiteral("AFDate_FormatEx(\"y\\\r") << int(Function::None);
    QTest::newRow("number overflow") << QStringLiteral("AFSpecial_Format(1e999);") << int(Function::None);
    QTest::newRow("no arguments") << QStringLiteral("AFSpecial_KeystrokeEx();") << int(Function::SpecialKeystrokeMask);
    QTest::newRow("empty date format") << QStringLiteral("AFDate_FormatEx('');") << int(Function::None);
    QTest::newRow("time format ex") << QStringLiteral("AFTime_FormatEx('HH:MM');") << int(Function::DateFormat);
    QTest::newRow("time keystroke index") << QStringLiteral("AFTime_Keystroke(0);") << int(Function::DateKeystroke);
    QTest::newRow("date index missing") << QStringLiteral("AFDate_Format();") << int(Function::None);
    QTest::newRow("special index missing") << QStringLiteral("AFSpecial_Format();") << int(Function::None);
    QTest::newRow("special index true") << QStringLiteral("AFSpecial_Format(true);") << int(Function::SpecialFormat);
    QTest::newRow("special index false") << QStringLiteral("AFSpecial_Format(false);") << int(Function::SpecialFormat);
    QTest::newRow("special index string") << QStringLiteral("AFSpecial_Format('3');") << int(Function::SpecialFormat);
    QTest::newRow("special index invalid string") << QStringLiteral("AFSpecial_Format('x');") << int(Function::None);
    QTest::newRow("date format true") << QStringLiteral("AFDate_FormatEx(true);") << int(Function::DateFormat);
    QTest::newRow("date format false") << QStringLiteral("AFDate_FormatEx(false);") << int(Function::DateFormat);
    QTest::newRow("date format number") << QStringLiteral("AFDate_FormatEx(1.5);") << int(Function::DateFormat);
}

void FormFieldFormatTest::test_parse()
{
    QFETCH(QString, script);
    QFETCH(int, function);

    QCOMPARE(int(PDFFormFieldFormat::parse(script).getFunction()), function);
}

void FormFieldFormatTest::test_number_format_data()
{
    QTest::addColumn<QString>("script");
    QTest::addColumn<QString>("value");
    QTest::addColumn<QString>("expected");
    QTest::addColumn<bool>("isRed");

    const QString euro = QString(QChar(0x20AC));

    QTest::newRow("style 3") << QStringLiteral("AFNumber_Format(2, 3, 0, 0, \"\", false);") << QStringLiteral("100200") << QStringLiteral("100200,00") << false;
    QTest::newRow("style 0") << QStringLiteral("AFNumber_Format(2, 0, 0, 0, \"\", false);") << QStringLiteral("1234567.891") << QStringLiteral("1,234,567.89") << false;
    QTest::newRow("style 1") << QStringLiteral("AFNumber_Format(2, 1, 0, 0, \"\", false);") << QStringLiteral("1234567.891") << QStringLiteral("1234567.89") << false;
    QTest::newRow("style 2") << QStringLiteral("AFNumber_Format(2, 2, 0, 0, \"\", false);") << QStringLiteral("1234567.891") << QStringLiteral("1.234.567,89") << false;
    QTest::newRow("style 4") << QStringLiteral("AFNumber_Format(2, 4, 0, 0, \"\", false);") << QStringLiteral("1234567.891") << QStringLiteral("1'234'567.89") << false;
    QTest::newRow("no decimals") << QStringLiteral("AFNumber_Format(0, 0, 0, 0, \"\", false);") << QStringLiteral("999.6") << QStringLiteral("1,000") << false;
    QTest::newRow("decimal comma value") << QStringLiteral("AFNumber_Format(2, 0, 0, 0, \"\", false);") << QStringLiteral("12,5") << QStringLiteral("12.50") << false;
    QTest::newRow("negative minus") << QStringLiteral("AFNumber_Format(2, 0, 0, 0, \"\", false);") << QStringLiteral("-1234.5") << QStringLiteral("-1,234.50") << false;
    QTest::newRow("negative red") << QStringLiteral("AFNumber_Format(2, 0, 1, 0, \"\", false);") << QStringLiteral("-1234.5") << QStringLiteral("1,234.50") << true;
    QTest::newRow("negative parentheses") << QStringLiteral("AFNumber_Format(2, 0, 2, 0, \"\", false);") << QStringLiteral("-1234.5") << QStringLiteral("(1,234.50)") << false;
    QTest::newRow("negative parentheses red") << QStringLiteral("AFNumber_Format(2, 0, 3, 0, \"\", false);") << QStringLiteral("-1234.5") << QStringLiteral("(1,234.50)") << true;
    QTest::newRow("positive red style") << QStringLiteral("AFNumber_Format(2, 0, 1, 0, \"\", false);") << QStringLiteral("1234.5") << QStringLiteral("1,234.50") << false;
    QTest::newRow("negative zero") << QStringLiteral("AFNumber_Format(2, 0, 0, 0, \"\", false);") << QStringLiteral("-0.001") << QStringLiteral("0.00") << false;
    QTest::newRow("currency prepend") << QStringLiteral("AFNumber_Format(2, 0, 0, 0, \"$\", true);") << QStringLiteral("-5") << QStringLiteral("-$5.00") << false;
    QTest::newRow("currency append") << QStringLiteral("AFNumber_Format(2, 2, 0, 0, \" \\u20AC\", false);") << QStringLiteral("5") << (QStringLiteral("5,00 ") + euro) << false;
    QTest::newRow("empty") << QStringLiteral("AFNumber_Format(2, 0, 0, 0, \"\", false);") << QString() << QString() << false;
    QTest::newRow("invalid") << QStringLiteral("AFNumber_Format(2, 0, 0, 0, \"\", false);") << QStringLiteral("abc") << QString() << false;
    QTest::newRow("negative parentheses currency") << QStringLiteral("AFNumber_Format(2, 0, 2, 0, \"$\", true);") << QStringLiteral("-5") << QStringLiteral("($5.00)") << false;
    QTest::newRow("missing arguments") << QStringLiteral("AFNumber_Format();") << QStringLiteral("1234.6") << QStringLiteral("1,235") << false;
    QTest::newRow("leading number") << QStringLiteral("AFNumber_Format(2, 0, 0, 0, \"\", false);") << QStringLiteral("12abc") << QStringLiteral("12.00") << false;
}

void FormFieldFormatTest::test_number_format()
{
    QFETCH(QString, script);
    QFETCH(QString, value);
    QFETCH(QString, expected);
    QFETCH(bool, isRed);

    PDFFormFieldFormat format = PDFFormFieldFormat::parse(script);
    QVERIFY(format.isFormat());
    QVERIFY(!format.isKeystroke());

    PDFFormFieldFormat::FormattedText formattedText = format.format(value);
    QCOMPARE(formattedText.text, expected);
    QCOMPARE(formattedText.textColor.isValid(), isRed);
    if (isRed)
    {
        QCOMPARE(formattedText.textColor, QColor(Qt::red));
    }
}

void FormFieldFormatTest::test_number_keystroke()
{
    PDFFormFieldFormat comma = PDFFormFieldFormat::parse(QStringLiteral("AFNumber_Keystroke(2, 3, 0, 0, \"\", false);"));
    QVERIFY(comma.isKeystroke());
    QVERIFY(!comma.isFormat());

    QVERIFY(comma.isKeystrokeAccepted(QString()));
    QVERIFY(comma.isKeystrokeAccepted(QStringLiteral("-")));
    QVERIFY(comma.isKeystrokeAccepted(QStringLiteral("100200,")));
    QVERIFY(comma.isKeystrokeAccepted(QStringLiteral("100200,5")));
    QVERIFY(!comma.isKeystrokeAccepted(QStringLiteral("100200.5")));
    QVERIFY(!comma.isKeystrokeAccepted(QStringLiteral("1,2,3")));
    QVERIFY(!comma.isKeystrokeAccepted(QStringLiteral("12a")));

    QCOMPARE(commitText(comma, QStringLiteral("100200")), QStringLiteral("100200"));
    QCOMPARE(commitText(comma, QStringLiteral("100200,50")), QStringLiteral("100200.5"));
    QCOMPARE(commitText(comma, QStringLiteral(",5")), QStringLiteral("0.5"));
    QCOMPARE(commitText(comma, QStringLiteral("12,")), QStringLiteral("12"));
    QCOMPARE(commitText(comma, QStringLiteral("-100000000")), QStringLiteral("-100000000"));
    QCOMPARE(commitText(comma, QString()), QString());
    QCOMPARE(commitText(comma, QStringLiteral(",")), QStringLiteral("<rejected>"));
    QCOMPARE(commitText(comma, QStringLiteral("-")), QStringLiteral("<rejected>"));
    QCOMPARE(commitText(comma, QStringLiteral("abc")), QStringLiteral("<rejected>"));

    QCOMPARE(comma.getEditText(QStringLiteral("100200.5")), QStringLiteral("100200,5"));
    QCOMPARE(comma.getEditText(QStringLiteral("text")), QStringLiteral("text"));

    PDFFormFieldFormat dot = PDFFormFieldFormat::parse(QStringLiteral("AFNumber_Keystroke(2, 0, 0, 0, \"\", false);"));
    QVERIFY(dot.isKeystrokeAccepted(QStringLiteral("1.5")));
    QVERIFY(!dot.isKeystrokeAccepted(QStringLiteral("1,5")));
    QCOMPARE(commitText(dot, QStringLiteral(" 1.50 ")), QStringLiteral("1.50"));
    QCOMPARE(dot.getEditText(QStringLiteral("1.5")), QStringLiteral("1.5"));
}

void FormFieldFormatTest::test_percent()
{
    PDFFormFieldFormat format = PDFFormFieldFormat::parse(QStringLiteral("AFPercent_Format(1, 0);"));
    QCOMPARE(format.format(QStringLiteral("0.1234")).text, QStringLiteral("12.3%"));
    QCOMPARE(format.format(QString()).text, QString());
    QCOMPARE(format.format(QStringLiteral("abc")).text, QStringLiteral("%"));

    PDFFormFieldFormat prepend = PDFFormFieldFormat::parse(QStringLiteral("AFPercent_Format(2, 2, true);"));
    QCOMPARE(prepend.format(QStringLiteral("12.3456")).text, QStringLiteral("%1.234,56"));

    PDFFormFieldFormat keystroke = PDFFormFieldFormat::parse(QStringLiteral("AFPercent_Keystroke(1, 2);"));
    QVERIFY(keystroke.isKeystrokeAccepted(QStringLiteral("0,5")));
    QVERIFY(!keystroke.isKeystrokeAccepted(QStringLiteral("0.5")));
    QCOMPARE(commitText(keystroke, QStringLiteral("0,5")), QStringLiteral("0.5"));
    QCOMPARE(keystroke.getEditText(QStringLiteral("0.5")), QStringLiteral("0,5"));
    QVERIFY(format.isFormat());
    QVERIFY(keystroke.isKeystroke());

    QCOMPARE(PDFFormFieldFormat::parse(QStringLiteral("AFPercent_Format(0, 0, \"x\");")).format(QStringLiteral("0.5")).text, QStringLiteral("%50"));
    QCOMPARE(PDFFormFieldFormat::parse(QStringLiteral("AFPercent_Format(0, 0, \"\");")).format(QStringLiteral("0.5")).text, QStringLiteral("50%"));
    QCOMPARE(PDFFormFieldFormat::parse(QStringLiteral("AFPercent_Format(0, 0, 1);")).format(QStringLiteral("0.5")).text, QStringLiteral("%50"));
    QCOMPARE(PDFFormFieldFormat::parse(QStringLiteral("AFPercent_Format(0, 0, 0);")).format(QStringLiteral("0.5")).text, QStringLiteral("50%"));
}

void FormFieldFormatTest::test_date_print()
{
    const QDateTime morning(QDate(2024, 3, 5), QTime(7, 8, 9));
    QCOMPARE(PDFFormFieldFormat::printDate(QStringLiteral("dddd, mmmm d, yyyy HH:MM:ss"), morning), QStringLiteral("Tuesday, March 5, 2024 07:08:09"));
    QCOMPARE(PDFFormFieldFormat::printDate(QStringLiteral("ddd dd.mm.yy h:M:s tt"), morning), QStringLiteral("Tue 05.03.24 7:8:9 am"));

    const QDateTime evening(QDate(2024, 3, 5), QTime(19, 0));
    QCOMPARE(PDFFormFieldFormat::printDate(QStringLiteral("\\m\\d yyy t"), evening), QStringLiteral("md 24y p"));

    const QDateTime midnight(QDate(2024, 3, 5), QTime(0, 30));
    QCOMPARE(PDFFormFieldFormat::printDate(QStringLiteral("hh:MM tt"), midnight), QStringLiteral("12:30 am"));
    QCOMPARE(PDFFormFieldFormat::printDate(QStringLiteral("yyyy\\"), morning), QStringLiteral("2024"));
    QCOMPARE(PDFFormFieldFormat::printDate(QStringLiteral("t"), morning), QStringLiteral("a"));

    std::optional<QDateTime> scanned = PDFFormFieldFormat::scanDate(QStringLiteral("mmmm d, yyyy h:MM tt"), QStringLiteral("march 5, 2024 7:08 PM"));
    QVERIFY(scanned.has_value());
    QCOMPARE(*scanned, QDateTime(QDate(2024, 3, 5), QTime(19, 8)));

    QVERIFY(!PDFFormFieldFormat::scanDate(QStringLiteral("dd.mm.yyyy"), QStringLiteral("5.3.2024")).has_value());
    QVERIFY(!PDFFormFieldFormat::scanDate(QStringLiteral("dd.mm.yyyy"), QStringLiteral("30.02.2024")).has_value());
}

void FormFieldFormatTest::test_date_format_data()
{
    QTest::addColumn<QString>("script");
    QTest::addColumn<QString>("value");
    QTest::addColumn<QString>("expected");

    QTest::newRow("exact") << QStringLiteral("AFDate_FormatEx(\"dd.mm.yyyy\");") << QStringLiteral("05.03.2024") << QStringLiteral("05.03.2024");
    QTest::newRow("short numbers") << QStringLiteral("AFDate_FormatEx(\"dd.mm.yyyy\");") << QStringLiteral("5.3.2024") << QStringLiteral("05.03.2024");
    QTest::newRow("iso") << QStringLiteral("AFDate_FormatEx(\"dd.mm.yyyy\");") << QStringLiteral("2024-03-05") << QStringLiteral("05.03.2024");
    QTest::newRow("invalid text kept") << QStringLiteral("AFDate_FormatEx(\"dd.mm.yyyy\");") << QStringLiteral("garbage") << QStringLiteral("garbage");
    QTest::newRow("invalid date kept") << QStringLiteral("AFDate_FormatEx(\"dd.mm.yyyy\");") << QStringLiteral("31.02.2024") << QStringLiteral("31.02.2024");
    QTest::newRow("empty") << QStringLiteral("AFDate_FormatEx(\"dd.mm.yyyy\");") << QString() << QString();
    QTest::newRow("month name") << QStringLiteral("AFDate_Format(10);") << QStringLiteral("March 5, 2024") << QStringLiteral("Mar 5, 2024");
    QTest::newRow("month number to name") << QStringLiteral("AFDate_FormatEx(\"d-mmm-yy\");") << QStringLiteral("5/3/24") << QStringLiteral("5-Mar-24");
    QTest::newRow("time 24 to 12") << QStringLiteral("AFTime_Format(1);") << QStringLiteral("14:05") << QStringLiteral("2:05 pm");
    QTest::newRow("time exact") << QStringLiteral("AFTime_Format(1);") << QStringLiteral("2:05 pm") << QStringLiteral("2:05 pm");
}

void FormFieldFormatTest::test_date_format()
{
    QFETCH(QString, script);
    QFETCH(QString, value);
    QFETCH(QString, expected);

    PDFFormFieldFormat format = PDFFormFieldFormat::parse(script);
    QVERIFY(format.isFormat());
    QCOMPARE(format.format(value).text, expected);
}

void FormFieldFormatTest::test_date_keystroke()
{
    PDFFormFieldFormat keystroke = PDFFormFieldFormat::parse(QStringLiteral("AFDate_KeystrokeEx(\"dd.mm.yyyy\");"));
    QVERIFY(keystroke.isKeystroke());
    QVERIFY(keystroke.isKeystrokeAccepted(QStringLiteral("3")));
    QVERIFY(keystroke.isKeystrokeAccepted(QStringLiteral("anything")));
    QCOMPARE(commitText(keystroke, QString()), QString());
    QCOMPARE(commitText(keystroke, QStringLiteral("01.02.2024")), QStringLiteral("01.02.2024"));
    QCOMPARE(commitText(keystroke, QStringLiteral("1.2.2024")), QStringLiteral("1.2.2024"));
    QCOMPARE(commitText(keystroke, QStringLiteral("31.02.2024")), QStringLiteral("<rejected>"));
    QCOMPARE(commitText(keystroke, QStringLiteral("tomorrow")), QStringLiteral("<rejected>"));
    QCOMPARE(commitText(keystroke, QStringLiteral("1.2.3.4")), QStringLiteral("<rejected>"));
}

void FormFieldFormatTest::test_special_format_data()
{
    QTest::addColumn<QString>("script");
    QTest::addColumn<QString>("value");
    QTest::addColumn<QString>("expected");

    QTest::newRow("zip") << QStringLiteral("AFSpecial_Format(0);") << QStringLiteral("12345") << QStringLiteral("12345");
    QTest::newRow("zip + 4") << QStringLiteral("AFSpecial_Format(1);") << QStringLiteral("123456789") << QStringLiteral("12345-6789");
    QTest::newRow("phone long") << QStringLiteral("AFSpecial_Format(2);") << QStringLiteral("5551234567") << QStringLiteral("(555) 123-4567");
    QTest::newRow("phone short") << QStringLiteral("AFSpecial_Format(2);") << QStringLiteral("5551234") << QStringLiteral("555-1234");
    QTest::newRow("ssn") << QStringLiteral("AFSpecial_Format(3);") << QStringLiteral("123-45-6789") << QStringLiteral("123-45-6789");
    QTest::newRow("empty") << QStringLiteral("AFSpecial_Format(0);") << QString() << QString();
}

void FormFieldFormatTest::test_special_format()
{
    QFETCH(QString, script);
    QFETCH(QString, value);
    QFETCH(QString, expected);

    PDFFormFieldFormat format = PDFFormFieldFormat::parse(script);
    QVERIFY(format.isFormat());
    QCOMPARE(format.format(value).text, expected);
}

void FormFieldFormatTest::test_special_keystroke()
{
    PDFFormFieldFormat zip = PDFFormFieldFormat::parse(QStringLiteral("AFSpecial_Keystroke(0);"));
    QVERIFY(zip.isKeystrokeAccepted(QStringLiteral("123")));
    QVERIFY(!zip.isKeystrokeAccepted(QStringLiteral("12a")));
    QVERIFY(!zip.isKeystrokeAccepted(QStringLiteral("123456")));
    QCOMPARE(commitText(zip, QStringLiteral("12345")), QStringLiteral("12345"));
    QCOMPARE(commitText(zip, QStringLiteral("1234")), QStringLiteral("<rejected>"));

    PDFFormFieldFormat phone = PDFFormFieldFormat::parse(QStringLiteral("AFSpecial_Keystroke(2);"));
    QVERIFY(phone.isKeystrokeAccepted(QStringLiteral("555-12")));
    QCOMPARE(commitText(phone, QStringLiteral("555-1234")), QStringLiteral("555-1234"));
    QCOMPARE(commitText(phone, QStringLiteral("(555) 123-4567")), QStringLiteral("(555) 123-4567"));
    QCOMPARE(commitText(phone, QStringLiteral("5551234567")), QStringLiteral("5551234567"));

    PDFFormFieldFormat mask = PDFFormFieldFormat::parse(QStringLiteral("AFSpecial_KeystrokeEx(\"99999-9999\");"));
    QVERIFY(mask.isKeystrokeAccepted(QStringLiteral("12345-")));
    QVERIFY(mask.isKeystrokeAccepted(QStringLiteral("123456")));
    QVERIFY(!mask.isKeystrokeAccepted(QStringLiteral("12345a")));
    QCOMPARE(commitText(mask, QStringLiteral("12345-6789")), QStringLiteral("12345-6789"));
    QCOMPARE(commitText(mask, QStringLiteral("123456789")), QStringLiteral("123456789"));
    QCOMPARE(commitText(mask, QStringLiteral("12345")), QStringLiteral("<rejected>"));

    QVERIFY(zip.isKeystroke());
    QVERIFY(mask.isKeystroke());
    QVERIFY(zip.isKeystrokeAccepted(QString()));
    QCOMPARE(commitText(zip, QStringLiteral("1234a")), QStringLiteral("<rejected>"));
    QVERIFY(phone.isKeystrokeAccepted(QStringLiteral("(555")));

    PDFFormFieldFormat letters = PDFFormFieldFormat::parse(QStringLiteral("AFSpecial_KeystrokeEx(\"AOX\");"));
    QVERIFY(letters.isKeystrokeAccepted(QStringLiteral("a1!")));
    QVERIFY(letters.isKeystrokeAccepted(QStringLiteral("ab")));
    QVERIFY(!letters.isKeystrokeAccepted(QStringLiteral("1")));
    QVERIFY(!letters.isKeystrokeAccepted(QStringLiteral("a!")));

    PDFFormFieldFormat emptyMask = PDFFormFieldFormat::parse(QStringLiteral("AFSpecial_KeystrokeEx();"));
    QVERIFY(emptyMask.isKeystrokeAccepted(QStringLiteral("anything")));
    QCOMPARE(commitText(emptyMask, QStringLiteral("anything")), QStringLiteral("anything"));
}

void FormFieldFormatTest::test_print_mask()
{
    QCOMPARE(PDFFormFieldFormat::printMask(QStringLiteral(">AAA-999"), QStringLiteral("ab1c23")), QStringLiteral("ABC-23"));
    QCOMPARE(PDFFormFieldFormat::printMask(QStringLiteral("?\\*X"), QStringLiteral("a#b")), QStringLiteral("a*b"));
    QCOMPARE(PDFFormFieldFormat::printMask(QStringLiteral("<*"), QStringLiteral("HeLLo")), QStringLiteral("hello"));
    QCOMPARE(PDFFormFieldFormat::printMask(QStringLiteral("AAAA"), QStringLiteral("1B_~b")), QStringLiteral("Bb"));
    QCOMPARE(PDFFormFieldFormat::printMask(QStringLiteral("X"), QStringLiteral("1")), QStringLiteral("1"));
    QCOMPARE(PDFFormFieldFormat::printMask(QStringLiteral("9"), QStringLiteral("abc")), QString());
    QCOMPARE(PDFFormFieldFormat::printMask(QStringLiteral(">A=A"), QStringLiteral("ab")), QStringLiteral("Ab"));
}

void FormFieldFormatTest::test_unsupported_function()
{
    PDFFormFieldFormat format = PDFFormFieldFormat::parse(QStringLiteral("event.value = 'x';"));
    QVERIFY(!format.isFormat());
    QVERIFY(!format.isKeystroke());
    QCOMPARE(format.format(QStringLiteral("text")).text, QStringLiteral("text"));
    QVERIFY(format.isKeystrokeAccepted(QStringLiteral("anything")));
    QCOMPARE(commitText(format, QStringLiteral("anything")), QStringLiteral("anything"));
    QCOMPARE(format.getEditText(QStringLiteral("1.5")), QStringLiteral("1.5"));
    QVERIFY(!PDFFormFieldFormat::parse(static_cast<const pdf::PDFAction*>(nullptr)).isFormat());
}

void FormFieldFormatTest::test_string_escapes()
{
    // Escapes: \n \r \t \b \f \v \x41 B \\ \" \q, line continuations (CR LF, CR, LF)
    PDFFormFieldFormat format = PDFFormFieldFormat::parse(QStringLiteral("AFDate_FormatEx(\"yyyy\\n\\r\\t\\b\\f\\v\\x41\\u0042\\\\\\\"\\q\\\r\n\\\rC\\\nD\");"));
    QCOMPARE(int(format.getFunction()), int(Function::DateFormat));
    QCOMPARE(format.format(QStringLiteral("2024-03-05")).text, QStringLiteral("2024\n\r\t\b\f\vAB\"qCD"));
}

void FormFieldFormatTest::test_make_number()
{
    QCOMPARE(PDFFormFieldFormat::makeNumber(QStringLiteral("1.e1")).value_or(0.0), 10.0);
    QCOMPARE(PDFFormFieldFormat::makeNumber(QStringLiteral("-.5")).value_or(0.0), -0.5);
    QVERIFY(!PDFFormFieldFormat::makeNumber(QStringLiteral("1e999")).has_value());
    QVERIFY(!PDFFormFieldFormat::makeNumber(QStringLiteral("abc")).has_value());
}

void FormFieldFormatTest::test_action()
{
    pdf::PDFObjectStorage storage;

    pdf::PDFObjectFactory javaScriptFactory;
    javaScriptFactory.beginDictionary();
    javaScriptFactory.beginDictionaryItem("S");
    javaScriptFactory << pdf::WrapName("JavaScript");
    javaScriptFactory.endDictionaryItem();
    javaScriptFactory.beginDictionaryItem("JS");
    javaScriptFactory << QStringLiteral("AFSpecial_Format(0);");
    javaScriptFactory.endDictionaryItem();
    javaScriptFactory.endDictionary();

    pdf::PDFActionPtr javaScriptAction = pdf::PDFAction::parse(&storage, javaScriptFactory.takeObject());
    QVERIFY(javaScriptAction);
    QCOMPARE(int(PDFFormFieldFormat::parse(javaScriptAction.data()).getFunction()), int(Function::SpecialFormat));

    pdf::PDFObjectFactory namedFactory;
    namedFactory.beginDictionary();
    namedFactory.beginDictionaryItem("S");
    namedFactory << pdf::WrapName("Named");
    namedFactory.endDictionaryItem();
    namedFactory.beginDictionaryItem("N");
    namedFactory << pdf::WrapName("NextPage");
    namedFactory.endDictionaryItem();
    namedFactory.endDictionary();

    pdf::PDFActionPtr namedAction = pdf::PDFAction::parse(&storage, namedFactory.takeObject());
    QVERIFY(namedAction);
    QCOMPARE(int(PDFFormFieldFormat::parse(namedAction.data()).getFunction()), int(Function::None));
}

void FormFieldFormatTest::test_scan_date()
{
    const int currentYear = QDate::currentDate().year();
    auto scan = [](const char* format, const char* text)
    {
        return PDFFormFieldFormat::scanDate(QString::fromLatin1(format), QString::fromLatin1(text)).value_or(QDateTime());
    };

    const QDateTime date(QDate(2024, 3, 5), QTime(0, 0));
    QCOMPARE(scan("dddd, d mmm yyyy", "Tuesday, 5 Mar 2024"), date);
    QCOMPARE(scan("ddd dd.mm.yy", "Tue 05.03.24"), date);
    QCOMPARE(scan("dd\\.mm\\.yyyy", "05.03.2024"), date);
    QCOMPARE(scan("yyyy\\", "2024"), QDateTime(QDate(2024, 1, 1), QTime(0, 0)));
    QCOMPARE(scan("y yyyy", "y 2024"), QDateTime(QDate(2024, 1, 1), QTime(0, 0)));
    QCOMPARE(scan("HH:MM:ss", "07:08:09"), QDateTime(QDate(currentYear, 1, 1), QTime(7, 8, 9)));
    QCOMPARE(scan("h:MM t", "7:08 p"), QDateTime(QDate(currentYear, 1, 1), QTime(19, 8)));
    QCOMPARE(scan("h:MM tt", "7:08 am"), QDateTime(QDate(currentYear, 1, 1), QTime(7, 8)));
    QCOMPARE(scan("h:MM tt", "0:05 am"), QDateTime());
    QCOMPARE(scan("h:MM tt", "13:05 pm"), QDateTime());
    QCOMPARE(scan("HH:MM", "25:00"), QDateTime());
}

void FormFieldFormatTest::test_guess_date()
{
    const int currentYear = QDate::currentDate().year();
    auto parse = [](const char* format, const char* text)
    {
        return PDFFormFieldFormat::parseDate(QString::fromLatin1(format), QString::fromLatin1(text)).value_or(QDateTime());
    };

    const QDateTime date(QDate(2024, 3, 5), QTime(0, 0));
    QCOMPARE(parse("dd\\.mm\\.yyyy", "5.3.2024"), date);
    QCOMPARE(parse("dddd dd.mm.yyyy", "Tuesday 5.3.2024"), date);
    QCOMPARE(parse("mmm d, yyyy", "at March April 5 2024"), date);
    QCOMPARE(parse("dd.mm.yyyy", "5 3"), QDateTime(QDate(currentYear, 3, 5), QTime(0, 0)));
    QCOMPARE(parse("h:MM tt", "14:05 PM"), QDateTime(QDate(currentYear, 1, 1), QTime(14, 5)));
    QCOMPARE(parse("h:MM tt", "2:05 p"), QDateTime(QDate(currentYear, 1, 1), QTime(14, 5)));
    QCOMPARE(parse("HH:MM:ss", "7 8 9"), QDateTime(QDate(currentYear, 1, 1), QTime(7, 8, 9)));
    QCOMPARE(parse("dd.mm.yyyy", "1.1.99999999999"), QDateTime());
    QCOMPARE(parse("HH:MM", "25 00"), QDateTime());
}

QTEST_APPLESS_MAIN(FormFieldFormatTest)

#include "tst_formfieldformattest.moc"
