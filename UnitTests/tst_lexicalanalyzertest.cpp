//    Copyright (C) 2018 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.


#include <QtTest>
#include <QMetaType>

#include "pdfparser.h"
#include "pdfconstants.h"

#include <regex>

class LexicalAnalyzerTest : public QObject
{
    Q_OBJECT

public:
    explicit LexicalAnalyzerTest();
    virtual ~LexicalAnalyzerTest() override;

private slots:
    void test_null();
    void test_numbers();
    void test_strings();
    void test_name();
    void test_bool();
    void test_ad();
    void test_command();
    void test_invalid_input();
    void test_header_regexp();

private:
    void scanWholeStream(const char* stream);
    void testTokens(const char* stream, const std::vector<pdf::PDFLexicalAnalyzer::Token>& tokens);

    QString getStringFromTokens(const std::vector<pdf::PDFLexicalAnalyzer::Token>& tokens);
};

LexicalAnalyzerTest::LexicalAnalyzerTest()
{

}

LexicalAnalyzerTest::~LexicalAnalyzerTest()
{

}

void LexicalAnalyzerTest::test_null()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("null", { Token(Type::Null) });
    testTokens("  null  ", { Token(Type::Null), Token(Type::EndOfFile) });
    testTokens("%null\n null %comment", { Token(Type::Null), Token(Type::EndOfFile) });
    testTokens("  \n\t null\n", { Token(Type::Null), Token(Type::EndOfFile) });
    testTokens(" null %and null\n null", { Token(Type::Null), Token(Type::Null) });
    testTokens(" null %and null\n null ", { Token(Type::Null), Token(Type::Null), Token(Type::EndOfFile) });
}

void LexicalAnalyzerTest::test_numbers()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("1 +2 -3 +40 -55", { Token(Type::Integer, 1), Token(Type::Integer, 2), Token(Type::Integer, -3), Token(Type::Integer, 40), Token(Type::Integer, -55) });
    testTokens(".0 0.1 3.5 -4. +5.0 -6.58 7.478", { Token(Type::Real, 0.0),  Token(Type::Real, 0.1),  Token(Type::Real, 3.5),  Token(Type::Real, -4.0),  Token(Type::Real, 5.0),  Token(Type::Real, -6.58),  Token(Type::Real, 7.478) });
    testTokens("1000000000000000000000000000", { Token(Type::Real, 1e27) });
}

void LexicalAnalyzerTest::test_strings()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("(Simple string)", { Token(Type::String, QByteArray("Simple string")) });
    testTokens("(String with (brackets))", { Token(Type::String, QByteArray("String with (brackets)")) });
    testTokens("(String with \\( unbalanced brackets \\(\\))", { Token(Type::String, QByteArray("String with ( unbalanced brackets ()")) });
    testTokens("()", { Token(Type::String, QByteArray("")) });
    testTokens("(Text with special character: \\n)", { Token(Type::String, QByteArray("Text with special character: \n")) });
    testTokens("(Text with special character: \\r)", { Token(Type::String, QByteArray("Text with special character: \r")) });
    testTokens("(Text with special character: \\t)", { Token(Type::String, QByteArray("Text with special character: \t")) });
    testTokens("(Text with special character: \\b)", { Token(Type::String, QByteArray("Text with special character: \b")) });
    testTokens("(Text with special character: \\f)", { Token(Type::String, QByteArray("Text with special character: \f")) });
    testTokens("(Text with special character: \\()", { Token(Type::String, QByteArray("Text with special character: (")) });
    testTokens("(Text with special character: \\))", { Token(Type::String, QByteArray("Text with special character: )")) });
    testTokens("(Text with special character: \\\\)", { Token(Type::String, QByteArray("Text with special character: \\")) });
    testTokens("(\53)", { Token(Type::String, QByteArray("+")) });
    testTokens("(\0533)", { Token(Type::String, QByteArray("+3")) });
    testTokens("(\053)", { Token(Type::String, QByteArray("+")) });
    testTokens("(\053053)", { Token(Type::String, QByteArray("+053")) });
    testTokens("(\5)", { Token(Type::String, QByteArray("\5")) });
    testTokens("<901FA3>", { Token(Type::String, QByteArray("\220\037\243")) });
    testTokens("<901fa3>", { Token(Type::String, QByteArray("\220\037\243")) });
    testTokens("<901fa>", { Token(Type::String, QByteArray("\220\037\240")) });
    testTokens("<901FA>", { Token(Type::String, QByteArray("\220\037\240")) });
    testTokens("<>", { Token(Type::String, QByteArray("")) });

    testTokens("(Simple string)(Simple string)", { Token(Type::String, QByteArray("Simple string")), Token(Type::String, QByteArray("Simple string")) });
    testTokens("(String with (brackets))(String with (brackets))", { Token(Type::String, QByteArray("String with (brackets)")), Token(Type::String, QByteArray("String with (brackets)")) });
    testTokens("(String with \\( unbalanced brackets \\(\\))(String with \\( unbalanced brackets \\(\\))", { Token(Type::String, QByteArray("String with ( unbalanced brackets ()")), Token(Type::String, QByteArray("String with ( unbalanced brackets ()")) });
    testTokens("()()", { Token(Type::String, QByteArray("")), Token(Type::String, QByteArray("")) });
    testTokens("(Text with special character: \\n)(Text with special character: \\n)", { Token(Type::String, QByteArray("Text with special character: \n")), Token(Type::String, QByteArray("Text with special character: \n")) });
    testTokens("(Text with special character: \\r)(Text with special character: \\r)", { Token(Type::String, QByteArray("Text with special character: \r")), Token(Type::String, QByteArray("Text with special character: \r")) });
    testTokens("(Text with special character: \\t)(Text with special character: \\t)", { Token(Type::String, QByteArray("Text with special character: \t")), Token(Type::String, QByteArray("Text with special character: \t")) });
    testTokens("(Text with special character: \\b)(Text with special character: \\b)", { Token(Type::String, QByteArray("Text with special character: \b")), Token(Type::String, QByteArray("Text with special character: \b")) });
    testTokens("(Text with special character: \\f)(Text with special character: \\f)", { Token(Type::String, QByteArray("Text with special character: \f")), Token(Type::String, QByteArray("Text with special character: \f")) });
    testTokens("(Text with special character: \\()(Text with special character: \\()", { Token(Type::String, QByteArray("Text with special character: (")), Token(Type::String, QByteArray("Text with special character: (")) });
    testTokens("(Text with special character: \\))(Text with special character: \\))", { Token(Type::String, QByteArray("Text with special character: )")), Token(Type::String, QByteArray("Text with special character: )")) });
    testTokens("(Text with special character: \\\\)(Text with special character: \\\\)", { Token(Type::String, QByteArray("Text with special character: \\")), Token(Type::String, QByteArray("Text with special character: \\")) });
    testTokens("(\53)(\53)", { Token(Type::String, QByteArray("+")), Token(Type::String, QByteArray("+")) });
    testTokens("(\0533)(\0533)", { Token(Type::String, QByteArray("+3")), Token(Type::String, QByteArray("+3")) });
    testTokens("(\053)(\053)", { Token(Type::String, QByteArray("+")), Token(Type::String, QByteArray("+")) });
    testTokens("(\053053)(\053053)", { Token(Type::String, QByteArray("+053")), Token(Type::String, QByteArray("+053")) });
    testTokens("(\5)(\5)", { Token(Type::String, QByteArray("\5")), Token(Type::String, QByteArray("\5")) });
    testTokens("<901FA3><901FA3>", { Token(Type::String, QByteArray("\220\037\243")), Token(Type::String, QByteArray("\220\037\243")) });
    testTokens("<901fa3><901fa3>", { Token(Type::String, QByteArray("\220\037\243")), Token(Type::String, QByteArray("\220\037\243")) });
    testTokens("<901fa><901fa>", { Token(Type::String, QByteArray("\220\037\240")), Token(Type::String, QByteArray("\220\037\240")) });
    testTokens("<901FA><901FA>", { Token(Type::String, QByteArray("\220\037\240")), Token(Type::String, QByteArray("\220\037\240")) });
    testTokens("<><>", { Token(Type::String, QByteArray("")), Token(Type::String, QByteArray("")) });
}

void LexicalAnalyzerTest::test_name()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("/Name123", { Token(Type::Name, QByteArray("Name123")) });
    testTokens("/VeryLongName", { Token(Type::Name, QByteArray("VeryLongName")) });
    testTokens("/A;Name_With^Various***Characters", { Token(Type::Name, QByteArray("A;Name_With^Various***Characters")) });
    testTokens("/1.2", { Token(Type::Name, QByteArray("1.2")) });
    testTokens("/$$", { Token(Type::Name, QByteArray("$$")) });
    testTokens("/@MatchedPattern", { Token(Type::Name, QByteArray("@MatchedPattern")) });
    testTokens("/.undefined", { Token(Type::Name, QByteArray(".undefined")) });
    testTokens("/The#20Major#20And#20The#20#23", { Token(Type::Name, QByteArray("The Major And The #")) });
    testTokens("/A#42", { Token(Type::Name, QByteArray("AB")) });
    testTokens("/#20", { Token(Type::Name, QByteArray(" ")) });
    testTokens("/#23#20#23/AB", { Token(Type::Name, QByteArray("# #")), Token(Type::Name, QByteArray("AB")) });

    testTokens("/Name123/Name123", { Token(Type::Name, QByteArray("Name123")), Token(Type::Name, QByteArray("Name123")) });
    testTokens("/VeryLongName/VeryLongName", { Token(Type::Name, QByteArray("VeryLongName")), Token(Type::Name, QByteArray("VeryLongName")) });
    testTokens("/A;Name_With^Various***Characters/A;Name_With^Various***Characters", { Token(Type::Name, QByteArray("A;Name_With^Various***Characters")), Token(Type::Name, QByteArray("A;Name_With^Various***Characters")) });
    testTokens("/1.2/1.2", { Token(Type::Name, QByteArray("1.2")), Token(Type::Name, QByteArray("1.2")) });
    testTokens("/$$/$$", { Token(Type::Name, QByteArray("$$")), Token(Type::Name, QByteArray("$$")) });
    testTokens("/@MatchedPattern/@MatchedPattern", { Token(Type::Name, QByteArray("@MatchedPattern")), Token(Type::Name, QByteArray("@MatchedPattern")) });
    testTokens("/.undefined/.undefined", { Token(Type::Name, QByteArray(".undefined")), Token(Type::Name, QByteArray(".undefined")) });
    testTokens("/The#20Major#20And#20The#20#23/The#20Major#20And#20The#20#23", { Token(Type::Name, QByteArray("The Major And The #")), Token(Type::Name, QByteArray("The Major And The #")) });
    testTokens("/A#42/A#42", { Token(Type::Name, QByteArray("AB")), Token(Type::Name, QByteArray("AB")) });
    testTokens("/#20/#20", { Token(Type::Name, QByteArray(" ")), Token(Type::Name, QByteArray(" ")) });
    testTokens("/#23#20#23/AB/#23#20#23/AB", { Token(Type::Name, QByteArray("# #")), Token(Type::Name, QByteArray("AB")), Token(Type::Name, QByteArray("# #")), Token(Type::Name, QByteArray("AB")) });
}

void LexicalAnalyzerTest::test_bool()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("true", { Token(Type::Boolean, true) });
    testTokens("false", { Token(Type::Boolean, false) });
    testTokens("true false true false", { Token(Type::Boolean, true), Token(Type::Boolean, false), Token(Type::Boolean, true), Token(Type::Boolean, false) });
}

void LexicalAnalyzerTest::test_ad()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("<<", { Token(Type::DictionaryStart) });
    testTokens("%comment\n<<", { Token(Type::DictionaryStart) });
    testTokens(">>", { Token(Type::DictionaryEnd) });
    testTokens("[", { Token(Type::ArrayStart) });
    testTokens("]", { Token(Type::ArrayEnd) });
}

void LexicalAnalyzerTest::test_command()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("command", { Token(Type::Command, QByteArray("command")) });
    testTokens("command1 command2", { Token(Type::Command, QByteArray("command1")), Token(Type::Command, QByteArray("command2")) });
}

void LexicalAnalyzerTest::test_invalid_input()
{
    QByteArray bigNumber(500, '0');
    bigNumber.front() = '1';
    bigNumber.back() = 0;

    QVERIFY_EXCEPTION_THROWN(scanWholeStream("(\\9adoctalnumber)"), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream("(\\)"), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream("123 456 +4-5"), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream("123 456 +"), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream("123 456 + 45"), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream(bigNumber.constData()), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream("/#Q1FF"), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream("/#1QFF"), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream("/# "), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream("<A bad hexadecimal string>"), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream("<1FA3"), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream("<1FA"), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream("> albatros"), pdf::PDFParserException);
    QVERIFY_EXCEPTION_THROWN(scanWholeStream(")"), pdf::PDFParserException);
}

void LexicalAnalyzerTest::test_header_regexp()
{
    std::regex regex(pdf::PDF_FILE_HEADER_REGEXP);

    for (const char* string : { "%PDF-1.4", "   %PDF-1.4abs", "%PDF-1.4", "%test %PDF %PDF-1.4", "%!PS-Adobe-3.0 PDF-1.4"})
    {
        std::cmatch cmatch;
        const bool matched = std::regex_search(string, string + strlen(string), cmatch, regex);
        QVERIFY(matched);

        if (matched)
        {
            QVERIFY(cmatch.size() == 3);
            QVERIFY(cmatch[1].matched || cmatch[2].matched);
        }
    }
}

void LexicalAnalyzerTest::scanWholeStream(const char* stream)
{
    pdf::PDFLexicalAnalyzer analyzer(stream, stream + strlen(stream));

    // Scan whole stream
    while (!analyzer.isAtEnd())
    {
        analyzer.fetch();
    }
}

void LexicalAnalyzerTest::testTokens(const char* stream, const std::vector<pdf::PDFLexicalAnalyzer::Token>& tokens)
{
    pdf::PDFLexicalAnalyzer analyzer(stream, stream + strlen(stream));

    std::vector<pdf::PDFLexicalAnalyzer::Token> scanned;
    scanned.reserve(tokens.size());

    // Scan whole stream
    while (!analyzer.isAtEnd())
    {
        scanned.emplace_back(analyzer.fetch());
    }

    // Format error message
    QString actual = getStringFromTokens(scanned);
    QString expected = getStringFromTokens(tokens);

    // Now, compare scanned tokens
    QVERIFY2(scanned == tokens, qPrintable(QString("stream: %1, actual = %2, expected = %3").arg(QString(stream), actual, expected)));
}

QString LexicalAnalyzerTest::getStringFromTokens(const std::vector<pdf::PDFLexicalAnalyzer::Token>& tokens)
{
    QStringList stringTokens;

    QMetaEnum metaEnum = QMetaEnum::fromType<pdf::PDFLexicalAnalyzer::TokenType>();
    Q_ASSERT(metaEnum.isValid());

    for (const pdf::PDFLexicalAnalyzer::Token& token : tokens)
    {
        QString tokenTypeAsString = metaEnum.valueToKey(static_cast<int>(token.type));

        if (!token.data.isValid())
        {
            stringTokens << tokenTypeAsString;
        }
        else
        {
            stringTokens << QString("%1(%2)").arg(tokenTypeAsString, token.data.toString());
        }
    }

    return QString("{ %1 }").arg(stringTokens.join(", "));
}

QTEST_APPLESS_MAIN(LexicalAnalyzerTest)

#include "tst_lexicalanalyzertest.moc"
