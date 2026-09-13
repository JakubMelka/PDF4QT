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

#include "pdfformfieldformat.h"
#include "pdfaction.h"

#include <QVariant>
#include <QRegularExpression>

#include <array>
#include <cmath>
#include <algorithm>
#include <functional>

namespace pdf
{

/// Helper functions for parsing of form field scripts and for
/// the implementation of the Acrobat form library functions.
class PDFFormFieldFormatHelper
{
public:
    PDFFormFieldFormatHelper() = delete;

    static constexpr int MAX_DECIMALS = 100;

    static constexpr std::array<const char*, 12> MONTH_NAMES = { "January", "February", "March", "April", "May", "June",
                                                                "July", "August", "September", "October", "November", "December" };

    static constexpr std::array<const char*, 7> DAY_NAMES = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

    /// Formats of AFDate_Format and AFTime_Format functions, selected by index
    static constexpr std::array<const char*, 14> DATE_FORMATS = { "m/d", "m/d/yy", "mm/dd/yy", "mm/yy", "d-mmm", "d-mmm-yy", "dd-mmm-yy", "yy-mm-dd",
                                                                 "mmm-yy", "mmmm-yy", "mmm d, yyyy", "mmmm d, yyyy", "m/d/yy h:MM tt", "m/d/yy HH:MM" };

    static constexpr std::array<const char*, 4> TIME_FORMATS = { "HH:MM", "h:MM tt", "HH:MM:ss", "h:MM:ss tt" };

    /// Masks of AFSpecial_Format function (zip code, zip code + 4, phone number, social security number)
    static constexpr std::array<const char*, 4> SPECIAL_FORMATS = { "99999", "99999-9999", "999-9999", "999-99-9999" };

    struct ScriptCall
    {
        QString function;
        QVariantList arguments;
    };

    static void skipWhitespace(const QString& text, qsizetype& position);
    static bool isIdentifierCharacter(QChar character, bool isFirst);

    /// Removes JavaScript comments from the script. If comment is not
    /// terminated, then empty string is returned.
    static QString removeComments(const QString& script);

    /// Converts number matched by the number regular expression to double. Forms
    /// like "1." or ".5" are normalized, so they can be converted.
    static std::optional<double> convertNumber(QString number);

    static const QRegularExpression& getNumberRegularExpression();
    static std::optional<char16_t> parseHexadecimalCode(const QString& text, qsizetype& position, qsizetype length);
    static std::optional<QVariant> parseStringLiteral(const QString& text, qsizetype& position);
    static std::optional<QVariant> parseLiteral(const QString& text, qsizetype& position);

    /// Parses script consisting of a single function call with literal arguments,
    /// for example "AFNumber_Format(2, 0, 0, 0, "", true);"
    static std::optional<ScriptCall> parseScriptCall(const QString& script);

    static double getNumberArgument(const QVariantList& arguments, qsizetype index, double defaultValue);
    static int getIntegerArgument(const QVariantList& arguments, qsizetype index, int defaultValue, int minimum, int maximum);
    static QString getStringArgument(const QVariantList& arguments, qsizetype index);
    static bool getBooleanArgument(const QVariantList& arguments, qsizetype index);

    /// Returns number of repetitions of the character at the given position (at most \p maximum)
    static qsizetype getRepeatCount(const QString& text, qsizetype position, qsizetype maximum);

    static bool isAsciiDigit(QChar character);
    static bool isAsciiLetter(QChar character);
    static bool isMaskMatched(const QString& value, const QString& mask);
    static std::optional<QString> checkMaskKeystroke(const QString& mask, const QString& value, bool willCommit);

    /// Checks value against the mask of AFSpecial_KeystrokeEx. Value is accepted, if it
    /// matches either the mask with literal characters removed, or the complete mask.
    static std::optional<QString> checkSpecialKeystroke(const QString& mask, const QString& value, bool willCommit);

    static QString getSpecialKeystrokeMask(int specialFormat, const QString& value);
    static bool isNumberKeystrokeAccepted(const QString& text, int separatorStyle, bool willCommit);
};

void PDFFormFieldFormatHelper::skipWhitespace(const QString& text, qsizetype& position)
{
    while (position < text.size() && text[position].isSpace())
    {
        ++position;
    }
}

bool PDFFormFieldFormatHelper::isIdentifierCharacter(QChar character, bool isFirst)
{
    if (character.isLetter() || character == QLatin1Char('_') || character == QLatin1Char('$'))
    {
        return true;
    }

    return !isFirst && character.isDigit();
}

QString PDFFormFieldFormatHelper::removeComments(const QString& script)
{
    QString result;
    result.reserve(script.size());

    QChar quote;
    for (qsizetype i = 0; i < script.size(); ++i)
    {
        const QChar character = script[i];
        const QChar nextCharacter = (i + 1 < script.size()) ? script[i + 1] : QChar();

        if (!quote.isNull())
        {
            result += character;

            if (character == QLatin1Char('\\') && !nextCharacter.isNull())
            {
                result += nextCharacter;
                ++i;
            }
            else if (character == quote)
            {
                quote = QChar();
            }
        }
        else if (character == QLatin1Char('"') || character == QLatin1Char('\''))
        {
            quote = character;
            result += character;
        }
        else if (character == QLatin1Char('/') && nextCharacter == QLatin1Char('/'))
        {
            while (i + 1 < script.size() && script[i + 1] != QLatin1Char('\n') && script[i + 1] != QLatin1Char('\r'))
            {
                ++i;
            }
            result += QLatin1Char(' ');
        }
        else if (character == QLatin1Char('/') && nextCharacter == QLatin1Char('*'))
        {
            const qsizetype commentEnd = script.indexOf(QLatin1String("*/"), i + 2);
            if (commentEnd == -1)
            {
                return QString();
            }

            i = commentEnd + 1;
            result += QLatin1Char(' ');
        }
        else
        {
            result += character;
        }
    }

    return result;
}

std::optional<double> PDFFormFieldFormatHelper::convertNumber(QString number)
{
    const qsizetype dotPosition = number.indexOf(QLatin1Char('.'));
    if (dotPosition != -1)
    {
        if (dotPosition + 1 == number.size() || !number[dotPosition + 1].isDigit())
        {
            number.remove(dotPosition, 1);
        }
        else if (dotPosition == 0 || !number[dotPosition - 1].isDigit())
        {
            number.insert(dotPosition, QLatin1Char('0'));
        }
    }

    bool ok = false;
    const double value = number.toDouble(&ok);
    if (!ok)
    {
        return std::nullopt;
    }

    return value;
}

const QRegularExpression& PDFFormFieldFormatHelper::getNumberRegularExpression()
{
    static const QRegularExpression expression(QStringLiteral("[+-]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?"));
    return expression;
}

std::optional<char16_t> PDFFormFieldFormatHelper::parseHexadecimalCode(const QString& text, qsizetype& position, qsizetype length)
{
    if (position + length > text.size())
    {
        return std::nullopt;
    }

    bool ok = false;
    const uint code = QStringView(text).mid(position, length).toUInt(&ok, 16);
    if (!ok)
    {
        return std::nullopt;
    }

    position += length;
    return static_cast<char16_t>(code);
}

std::optional<QVariant> PDFFormFieldFormatHelper::parseStringLiteral(const QString& text, qsizetype& position)
{
    const QChar quote = text[position++];
    QString value;

    while (position < text.size())
    {
        const QChar character = text[position++];

        if (character == quote)
        {
            return QVariant(value);
        }

        if (character == QLatin1Char('\n') || character == QLatin1Char('\r'))
        {
            return std::nullopt;
        }

        if (character != QLatin1Char('\\'))
        {
            value += character;
            continue;
        }

        if (position >= text.size())
        {
            return std::nullopt;
        }

        const QChar escapedCharacter = text[position++];
        switch (escapedCharacter.unicode())
        {
            case u'n':
                value += QLatin1Char('\n');
                break;

            case u'r':
                value += QLatin1Char('\r');
                break;

            case u't':
                value += QLatin1Char('\t');
                break;

            case u'b':
                value += QLatin1Char('\b');
                break;

            case u'f':
                value += QLatin1Char('\f');
                break;

            case u'v':
                value += QLatin1Char('\v');
                break;

            case u'x':
            case u'u':
            {
                std::optional<char16_t> code = parseHexadecimalCode(text, position, escapedCharacter == QLatin1Char('x') ? 2 : 4);
                if (!code)
                {
                    return std::nullopt;
                }
                value += QChar(*code);
                break;
            }

            case u'\r':
            {
                // Line continuation
                if (position < text.size() && text[position] == QLatin1Char('\n'))
                {
                    ++position;
                }
                break;
            }

            case u'\n':
                break;

            default:
                value += escapedCharacter;
                break;
        }
    }

    return std::nullopt;
}

std::optional<QVariant> PDFFormFieldFormatHelper::parseLiteral(const QString& text, qsizetype& position)
{
    if (position >= text.size())
    {
        return std::nullopt;
    }

    const QChar character = text[position];
    if (character == QLatin1Char('"') || character == QLatin1Char('\''))
    {
        return parseStringLiteral(text, position);
    }

    if (isIdentifierCharacter(character, true))
    {
        qsizetype end = position;
        while (end < text.size() && isIdentifierCharacter(text[end], false))
        {
            ++end;
        }

        const QStringView identifier = QStringView(text).mid(position, end - position);
        position = end;

        if (identifier == u"true")
        {
            return QVariant(true);
        }
        if (identifier == u"false")
        {
            return QVariant(false);
        }

        // Variables and other identifiers are not literals
        return std::nullopt;
    }

    QRegularExpressionMatch match = getNumberRegularExpression().match(text, position, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    if (!match.hasMatch())
    {
        return std::nullopt;
    }

    position += match.capturedLength();
    std::optional<double> number = convertNumber(match.captured());
    if (!number)
    {
        return std::nullopt;
    }

    return QVariant(*number);
}

std::optional<PDFFormFieldFormatHelper::ScriptCall> PDFFormFieldFormatHelper::parseScriptCall(const QString& script)
{
    const QString text = removeComments(script);

    ScriptCall call;
    qsizetype position = 0;
    skipWhitespace(text, position);

    const qsizetype identifierStart = position;
    while (position < text.size() && isIdentifierCharacter(text[position], position == identifierStart))
    {
        ++position;
    }

    if (position == identifierStart)
    {
        return std::nullopt;
    }

    call.function = text.mid(identifierStart, position - identifierStart);
    skipWhitespace(text, position);

    if (position >= text.size() || text[position] != QLatin1Char('('))
    {
        return std::nullopt;
    }

    ++position;
    skipWhitespace(text, position);

    if (position < text.size() && text[position] == QLatin1Char(')'))
    {
        ++position;
    }
    else
    {
        while (true)
        {
            std::optional<QVariant> literal = parseLiteral(text, position);
            if (!literal)
            {
                return std::nullopt;
            }

            call.arguments.push_back(*literal);
            skipWhitespace(text, position);

            if (position >= text.size())
            {
                return std::nullopt;
            }

            const QChar separator = text[position++];
            if (separator == QLatin1Char(')'))
            {
                break;
            }

            if (separator != QLatin1Char(','))
            {
                return std::nullopt;
            }

            skipWhitespace(text, position);
        }
    }

    skipWhitespace(text, position);
    if (position < text.size() && text[position] == QLatin1Char(';'))
    {
        ++position;
    }
    skipWhitespace(text, position);

    if (position != text.size())
    {
        return std::nullopt;
    }

    return call;
}

double PDFFormFieldFormatHelper::getNumberArgument(const QVariantList& arguments, qsizetype index, double defaultValue)
{
    if (index >= arguments.size())
    {
        return defaultValue;
    }

    const QVariant& argument = arguments[index];
    switch (argument.typeId())
    {
        case QMetaType::Bool:
            return argument.toBool() ? 1.0 : 0.0;

        case QMetaType::QString:
            return PDFFormFieldFormat::makeNumber(argument.toString()).value_or(defaultValue);

        default:
            return argument.toDouble();
    }
}

int PDFFormFieldFormatHelper::getIntegerArgument(const QVariantList& arguments, qsizetype index, int defaultValue, int minimum, int maximum)
{
    const double value = getNumberArgument(arguments, index, defaultValue);
    return static_cast<int>(std::floor(qBound(double(minimum), value, double(maximum))));
}

QString PDFFormFieldFormatHelper::getStringArgument(const QVariantList& arguments, qsizetype index)
{
    if (index >= arguments.size())
    {
        return QString();
    }

    const QVariant& argument = arguments[index];
    switch (argument.typeId())
    {
        case QMetaType::Bool:
            return argument.toBool() ? QStringLiteral("true") : QStringLiteral("false");

        case QMetaType::Double:
            return QString::number(argument.toDouble(), 'g', QLocale::FloatingPointShortest);

        default:
            return argument.toString();
    }
}

bool PDFFormFieldFormatHelper::getBooleanArgument(const QVariantList& arguments, qsizetype index)
{
    if (index >= arguments.size())
    {
        return false;
    }

    const QVariant& argument = arguments[index];
    switch (argument.typeId())
    {
        case QMetaType::Bool:
            return argument.toBool();

        case QMetaType::QString:
            return !argument.toString().isEmpty();

        default:
            return argument.toDouble() != 0.0;
    }
}

qsizetype PDFFormFieldFormatHelper::getRepeatCount(const QString& text, qsizetype position, qsizetype maximum)
{
    const QChar character = text[position];

    qsizetype count = 1;
    while (count < maximum && position + count < text.size() && text[position + count] == character)
    {
        ++count;
    }

    return count;
}

bool PDFFormFieldFormatHelper::isAsciiDigit(QChar character)
{
    return character >= QLatin1Char('0') && character <= QLatin1Char('9');
}

bool PDFFormFieldFormatHelper::isAsciiLetter(QChar character)
{
    return (character >= QLatin1Char('a') && character <= QLatin1Char('z')) || (character >= QLatin1Char('A') && character <= QLatin1Char('Z'));
}

bool PDFFormFieldFormatHelper::isMaskMatched(const QString& value, const QString& mask)
{
    Q_ASSERT(value.size() <= mask.size());

    for (qsizetype i = 0; i < value.size(); ++i)
    {
        const QChar maskCharacter = mask[i];
        const QChar character = value[i];

        switch (maskCharacter.unicode())
        {
            case u'9':
                if (!isAsciiDigit(character))
                {
                    return false;
                }
                break;

            case u'A':
                if (!isAsciiLetter(character))
                {
                    return false;
                }
                break;

            case u'O':
                if (!isAsciiDigit(character) && !isAsciiLetter(character))
                {
                    return false;
                }
                break;

            case u'X':
                break;

            default:
                if (character != maskCharacter)
                {
                    return false;
                }
                break;
        }
    }

    return true;
}

std::optional<QString> PDFFormFieldFormatHelper::checkMaskKeystroke(const QString& mask, const QString& value, bool willCommit)
{
    if (value.isEmpty())
    {
        return value;
    }

    if (value.size() > mask.size())
    {
        return std::nullopt;
    }

    if (willCommit)
    {
        if (value.size() < mask.size() || !isMaskMatched(value, mask))
        {
            return std::nullopt;
        }

        return value;
    }

    if (!isMaskMatched(value, mask.left(value.size())))
    {
        return std::nullopt;
    }

    return value;
}

std::optional<QString> PDFFormFieldFormatHelper::checkSpecialKeystroke(const QString& mask, const QString& value, bool willCommit)
{
    if (mask.isEmpty())
    {
        return value;
    }

    QString simplifiedMask;
    for (QChar character : mask)
    {
        if (character == QLatin1Char('9') || character == QLatin1Char('A') || character == QLatin1Char('O') || character == QLatin1Char('X'))
        {
            simplifiedMask += character;
        }
    }

    if (std::optional<QString> result = checkMaskKeystroke(simplifiedMask, value, willCommit))
    {
        return result;
    }

    return checkMaskKeystroke(mask, value, willCommit);
}

QString PDFFormFieldFormatHelper::getSpecialKeystrokeMask(int specialFormat, const QString& value)
{
    if (specialFormat == 2)
    {
        if (value.size() > 8 || value.startsWith(QLatin1Char('(')))
        {
            return QStringLiteral("(999) 999-9999");
        }

        return QStringLiteral("999-9999");
    }

    return QString::fromLatin1(SPECIAL_FORMATS[specialFormat]);
}

bool PDFFormFieldFormatHelper::isNumberKeystrokeAccepted(const QString& text, int separatorStyle, bool willCommit)
{
    static const QRegularExpression commaEditExpression(QStringLiteral("^[+-]?\\d*,?\\d*$"));
    static const QRegularExpression commaCommitExpression(QStringLiteral("^[+-]?(?:\\d+(?:,\\d*)?|,\\d+)$"));
    static const QRegularExpression dotEditExpression(QStringLiteral("^[+-]?\\d*\\.?\\d*$"));
    static const QRegularExpression dotCommitExpression(QStringLiteral("^[+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)$"));

    const bool isComma = separatorStyle > 1;
    const QRegularExpression& expression = willCommit ? (isComma ? commaCommitExpression : dotCommitExpression)
                                                      : (isComma ? commaEditExpression : dotEditExpression);
    return expression.match(text).hasMatch();
}

PDFFormFieldFormat PDFFormFieldFormat::parse(const PDFAction* action)
{
    if (const PDFActionJavaScript* javaScriptAction = dynamic_cast<const PDFActionJavaScript*>(action))
    {
        return parse(javaScriptAction->getJavaScript());
    }

    return PDFFormFieldFormat();
}

PDFFormFieldFormat PDFFormFieldFormat::parse(const QString& script)
{
    PDFFormFieldFormat result;

    std::optional<PDFFormFieldFormatHelper::ScriptCall> call = PDFFormFieldFormatHelper::parseScriptCall(script);
    if (!call)
    {
        return result;
    }

    const QString& name = call->function;
    const QVariantList& arguments = call->arguments;

    if (name == u"AFNumber_Format" || name == u"AFNumber_Keystroke")
    {
        result.m_function = (name == u"AFNumber_Format") ? Function::NumberFormat : Function::NumberKeystroke;
        result.m_decimals = PDFFormFieldFormatHelper::getIntegerArgument(arguments, 0, 0, 0, PDFFormFieldFormatHelper::MAX_DECIMALS);
        result.m_separatorStyle = PDFFormFieldFormatHelper::getIntegerArgument(arguments, 1, 0, 0, 4);
        result.m_negativeStyle = PDFFormFieldFormatHelper::getIntegerArgument(arguments, 2, 0, 0, 3);
        result.m_currency = PDFFormFieldFormatHelper::getStringArgument(arguments, 4);
        result.m_currencyPrepend = PDFFormFieldFormatHelper::getBooleanArgument(arguments, 5);
    }
    else if (name == u"AFPercent_Format" || name == u"AFPercent_Keystroke")
    {
        result.m_function = (name == u"AFPercent_Format") ? Function::PercentFormat : Function::PercentKeystroke;
        result.m_decimals = PDFFormFieldFormatHelper::getIntegerArgument(arguments, 0, 0, 0, PDFFormFieldFormatHelper::MAX_DECIMALS);
        result.m_separatorStyle = PDFFormFieldFormatHelper::getIntegerArgument(arguments, 1, 0, 0, 4);
        result.m_percentPrepend = PDFFormFieldFormatHelper::getBooleanArgument(arguments, 2);
    }
    else if (name == u"AFDate_FormatEx" || name == u"AFTime_FormatEx" || name == u"AFDate_KeystrokeEx" || name == u"AFTime_KeystrokeEx")
    {
        result.m_dateFormat = PDFFormFieldFormatHelper::getStringArgument(arguments, 0);
        if (result.m_dateFormat.isEmpty())
        {
            return PDFFormFieldFormat();
        }

        result.m_function = name.endsWith(u"FormatEx") ? Function::DateFormat : Function::DateKeystroke;
    }
    else if (name == u"AFDate_Format" || name == u"AFTime_Format" || name == u"AFDate_Keystroke" || name == u"AFTime_Keystroke")
    {
        const bool isTime = name.startsWith(u"AFTime");
        const int count = static_cast<int>(isTime ? PDFFormFieldFormatHelper::TIME_FORMATS.size() : PDFFormFieldFormatHelper::DATE_FORMATS.size());
        const int index = PDFFormFieldFormatHelper::getIntegerArgument(arguments, 0, -1, -1, count);
        if (index < 0 || index >= count)
        {
            return PDFFormFieldFormat();
        }

        result.m_dateFormat = QString::fromLatin1(isTime ? PDFFormFieldFormatHelper::TIME_FORMATS[index] : PDFFormFieldFormatHelper::DATE_FORMATS[index]);
        result.m_function = name.endsWith(u"_Format") ? Function::DateFormat : Function::DateKeystroke;
    }
    else if (name == u"AFSpecial_Format" || name == u"AFSpecial_Keystroke")
    {
        const int count = static_cast<int>(PDFFormFieldFormatHelper::SPECIAL_FORMATS.size());
        const int index = PDFFormFieldFormatHelper::getIntegerArgument(arguments, 0, -1, -1, count);
        if (index < 0 || index >= count)
        {
            return PDFFormFieldFormat();
        }

        result.m_specialFormat = index;
        result.m_function = (name == u"AFSpecial_Format") ? Function::SpecialFormat : Function::SpecialKeystroke;
    }
    else if (name == u"AFSpecial_KeystrokeEx")
    {
        result.m_mask = PDFFormFieldFormatHelper::getStringArgument(arguments, 0);
        result.m_function = Function::SpecialKeystrokeMask;
    }

    return result;
}

bool PDFFormFieldFormat::isFormat() const
{
    switch (m_function)
    {
        case Function::NumberFormat:
        case Function::PercentFormat:
        case Function::DateFormat:
        case Function::SpecialFormat:
            return true;

        default:
            return false;
    }
}

bool PDFFormFieldFormat::isKeystroke() const
{
    switch (m_function)
    {
        case Function::NumberKeystroke:
        case Function::PercentKeystroke:
        case Function::DateKeystroke:
        case Function::SpecialKeystroke:
        case Function::SpecialKeystrokeMask:
            return true;

        default:
            return false;
    }
}

PDFFormFieldFormat::FormattedText PDFFormFieldFormat::format(const QString& value) const
{
    FormattedText result;
    result.text = value;

    switch (m_function)
    {
        case Function::NumberFormat:
        {
            std::optional<double> number = makeNumber(value);
            if (!number)
            {
                result.text.clear();
                break;
            }

            const QString signedText = formatNumber(*number, m_decimals, m_separatorStyle);
            const bool isNegative = signedText.startsWith(QLatin1Char('-'));
            const bool hasParentheses = isNegative && (m_negativeStyle == 2 || m_negativeStyle == 3);

            QString text;
            if (isNegative && m_currencyPrepend && m_negativeStyle == 0)
            {
                text += QLatin1Char('-');
            }
            if (hasParentheses)
            {
                text += QLatin1Char('(');
            }
            if (m_currencyPrepend)
            {
                text += m_currency;
            }
            if (isNegative && (m_negativeStyle != 0 || m_currencyPrepend))
            {
                // Minus sign is already written, or it is replaced by parentheses or red color
                text += signedText.mid(1);
            }
            else
            {
                text += signedText;
            }
            if (!m_currencyPrepend)
            {
                text += m_currency;
            }
            if (hasParentheses)
            {
                text += QLatin1Char(')');
            }

            result.text = text;
            if (isNegative && (m_negativeStyle == 1 || m_negativeStyle == 3))
            {
                result.textColor = Qt::red;
            }
            break;
        }

        case Function::PercentFormat:
        {
            if (value.trimmed().isEmpty())
            {
                result.text.clear();
                break;
            }

            std::optional<double> number = makeNumber(value);
            const QString text = number ? formatNumber(*number * 100.0, m_decimals, m_separatorStyle) : QString();
            result.text = m_percentPrepend ? QLatin1Char('%') + text : text + QLatin1Char('%');
            break;
        }

        case Function::DateFormat:
        {
            if (value.isEmpty())
            {
                break;
            }

            if (std::optional<QDateTime> dateTime = parseDate(m_dateFormat, value))
            {
                result.text = printDate(m_dateFormat, *dateTime);
            }
            break;
        }

        case Function::SpecialFormat:
        {
            if (value.isEmpty())
            {
                break;
            }

            QString mask = QString::fromLatin1(PDFFormFieldFormatHelper::SPECIAL_FORMATS[m_specialFormat]);
            if (m_specialFormat == 2 && printMask(QStringLiteral("9999999999"), value).size() >= 10)
            {
                mask = QStringLiteral("(999) 999-9999");
            }

            result.text = printMask(mask, value);
            break;
        }

        default:
            break;
    }

    return result;
}

bool PDFFormFieldFormat::isKeystrokeAccepted(const QString& text) const
{
    switch (m_function)
    {
        case Function::NumberKeystroke:
        case Function::PercentKeystroke:
        {
            const QString trimmedText = text.trimmed();
            return text.isEmpty() || PDFFormFieldFormatHelper::isNumberKeystrokeAccepted(trimmedText, m_separatorStyle, false);
        }

        case Function::SpecialKeystroke:
            return PDFFormFieldFormatHelper::checkSpecialKeystroke(PDFFormFieldFormatHelper::getSpecialKeystrokeMask(m_specialFormat, text), text, false).has_value();

        case Function::SpecialKeystrokeMask:
            return PDFFormFieldFormatHelper::checkSpecialKeystroke(m_mask, text, false).has_value();

        default:
            return true;
    }
}

std::optional<QString> PDFFormFieldFormat::commit(const QString& text) const
{
    switch (m_function)
    {
        case Function::NumberKeystroke:
        case Function::PercentKeystroke:
        {
            if (text.isEmpty())
            {
                return text;
            }

            const QString trimmedText = text.trimmed();
            if (!PDFFormFieldFormatHelper::isNumberKeystrokeAccepted(trimmedText, m_separatorStyle, true))
            {
                return std::nullopt;
            }

            if (m_separatorStyle > 1)
            {
                // Value with decimal comma is stored as a number
                std::optional<double> number = makeNumber(trimmedText);
                Q_ASSERT(number.has_value());
                return QString::number(*number, 'f', QLocale::FloatingPointShortest);
            }

            return trimmedText;
        }

        case Function::DateKeystroke:
        {
            if (text.isEmpty() || parseDate(m_dateFormat, text))
            {
                return text;
            }

            return std::nullopt;
        }

        case Function::SpecialKeystroke:
            return PDFFormFieldFormatHelper::checkSpecialKeystroke(PDFFormFieldFormatHelper::getSpecialKeystrokeMask(m_specialFormat, text), text, true);

        case Function::SpecialKeystrokeMask:
            return PDFFormFieldFormatHelper::checkSpecialKeystroke(m_mask, text, true);

        default:
            return text;
    }
}

QString PDFFormFieldFormat::getEditText(const QString& value) const
{
    if ((m_function == Function::NumberKeystroke || m_function == Function::PercentKeystroke) && m_separatorStyle > 1)
    {
        static const QRegularExpression storedNumberExpression(QStringLiteral("^[+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)$"));
        if (storedNumberExpression.match(value).hasMatch())
        {
            QString text = value;
            text.replace(QLatin1Char('.'), QLatin1Char(','));
            return text;
        }
    }

    return value;
}

QString PDFFormFieldFormat::formatNumber(double value, int decimals, int separatorStyle)
{
    static constexpr std::array<std::array<const char*, 2>, 5> SEPARATORS = { { { ",", "." }, { "", "." }, { ".", "," }, { "", "," }, { "'", "." } } };

    separatorStyle = qBound(0, separatorStyle, 4);
    decimals = qBound(0, decimals, PDFFormFieldFormatHelper::MAX_DECIMALS);

    const QString text = QString::number(std::abs(value), 'f', decimals);
    const qsizetype dotPosition = text.indexOf(QLatin1Char('.'));
    const QString integerPart = (dotPosition != -1) ? text.left(dotPosition) : text;
    const QString fractionPart = (dotPosition != -1) ? text.mid(dotPosition + 1) : QString();

    const bool isZero = std::all_of(text.cbegin(), text.cend(), [](QChar character) { return !character.isDigit() || character == QLatin1Char('0'); });

    QString result;
    if (value < 0.0 && !isZero)
    {
        result += QLatin1Char('-');
    }

    const QString thousandsSeparator = QString::fromLatin1(SEPARATORS[separatorStyle][0]);
    for (qsizetype i = 0; i < integerPart.size(); ++i)
    {
        if (i > 0 && (integerPart.size() - i) % 3 == 0)
        {
            result += thousandsSeparator;
        }
        result += integerPart[i];
    }

    if (!fractionPart.isEmpty())
    {
        result += QString::fromLatin1(SEPARATORS[separatorStyle][1]);
        result += fractionPart;
    }

    return result;
}

std::optional<double> PDFFormFieldFormat::makeNumber(const QString& text)
{
    QString trimmedText = text.trimmed();
    const qsizetype commaPosition = trimmedText.indexOf(QLatin1Char(','));
    if (commaPosition != -1)
    {
        trimmedText[commaPosition] = QLatin1Char('.');
    }

    QRegularExpressionMatch match = PDFFormFieldFormatHelper::getNumberRegularExpression().match(trimmedText, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    if (!match.hasMatch())
    {
        return std::nullopt;
    }

    return PDFFormFieldFormatHelper::convertNumber(match.captured());
}

QString PDFFormFieldFormat::printDate(const QString& format, const QDateTime& dateTime)
{
    const QDate date = dateTime.date();
    const QTime time = dateTime.time();

    auto number = [](int value, qsizetype count)
    {
        return QString::number(value).rightJustified(count, QLatin1Char('0'));
    };

    QString result;
    qsizetype position = 0;
    while (position < format.size())
    {
        const QChar character = format[position];

        switch (character.unicode())
        {
            case u'\\':
            {
                if (position + 1 < format.size())
                {
                    result += format[position + 1];
                }
                position += 2;
            }
            break;

            case u'm':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 4);
                const QString monthName = QString::fromLatin1(PDFFormFieldFormatHelper::MONTH_NAMES[date.month() - 1]);
                result += (count == 4) ? monthName : (count == 3) ? monthName.left(3) : number(date.month(), count);
                position += count;
            }
            break;

            case u'd':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 4);
                const QString dayName = QString::fromLatin1(PDFFormFieldFormatHelper::DAY_NAMES[date.dayOfWeek() % 7]);
                result += (count == 4) ? dayName : (count == 3) ? dayName.left(3) : number(date.day(), count);
                position += count;
            }
            break;

            case u'y':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 4);
                if (count == 4)
                {
                    result += number(date.year(), 4);
                    position += 4;
                }
                else if (count >= 2)
                {
                    result += number(date.year() % 100, 2);
                    position += 2;
                }
                else
                {
                    result += character;
                    position += 1;
                }
            }
            break;

            case u'H':
            case u'h':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 2);
                const int hour = (character == QLatin1Char('H')) ? time.hour() : 1 + (time.hour() + 11) % 12;
                result += number(hour, count);
                position += count;
            }
            break;

            case u'M':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 2);
                result += number(time.minute(), count);
                position += count;
            }
            break;

            case u's':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 2);
                result += number(time.second(), count);
                position += count;
            }
            break;

            case u't':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 2);
                const bool isMorning = time.hour() < 12;
                result += (count == 2) ? (isMorning ? QStringLiteral("am") : QStringLiteral("pm")) : (isMorning ? QStringLiteral("a") : QStringLiteral("p"));
                position += count;
            }
            break;

            default:
                result += character;
                ++position;
                break;
        }
    }

    return result;
}

std::optional<QDateTime> PDFFormFieldFormat::scanDate(const QString& format, const QString& text)
{
    enum class Component
    {
        Ignored,
        Year,
        ShortYear,
        Month,
        MonthName,
        Day,
        Hour,
        Hour12,
        Minute,
        Second,
        AmPm
    };

    auto join = [](auto begin, auto end, qsizetype length)
    {
        QStringList names;
        for (auto it = begin; it != end; ++it)
        {
            names << QString::fromLatin1(*it).left(length);
        }
        return QStringLiteral("(") + names.join(QLatin1Char('|')) + QStringLiteral(")");
    };

    const QString numberPattern[3] = { QString(), QStringLiteral("(\\d{1,2})"), QStringLiteral("(\\d{2})") };

    std::vector<Component> components;
    QString pattern = QStringLiteral("^");
    qsizetype position = 0;
    while (position < format.size())
    {
        const QChar character = format[position];

        switch (character.unicode())
        {
            case u'\\':
            {
                if (position + 1 < format.size())
                {
                    pattern += QRegularExpression::escape(QString(format[position + 1]));
                }
                position += 2;
            }
            break;

            case u'm':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 4);
                pattern += (count >= 3) ? join(PDFFormFieldFormatHelper::MONTH_NAMES.cbegin(), PDFFormFieldFormatHelper::MONTH_NAMES.cend(), count == 3 ? 3 : 20) : numberPattern[count];
                components.push_back((count >= 3) ? Component::MonthName : Component::Month);
                position += count;
            }
            break;

            case u'd':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 4);
                pattern += (count >= 3) ? join(PDFFormFieldFormatHelper::DAY_NAMES.cbegin(), PDFFormFieldFormatHelper::DAY_NAMES.cend(), count == 3 ? 3 : 20) : numberPattern[count];
                components.push_back((count >= 3) ? Component::Ignored : Component::Day);
                position += count;
            }
            break;

            case u'y':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 4);
                if (count == 4)
                {
                    pattern += QStringLiteral("(\\d{4})");
                    components.push_back(Component::Year);
                    position += 4;
                }
                else if (count >= 2)
                {
                    pattern += QStringLiteral("(\\d{2})");
                    components.push_back(Component::ShortYear);
                    position += 2;
                }
                else
                {
                    pattern += character;
                    position += 1;
                }
            }
            break;

            case u'H':
            case u'h':
            case u'M':
            case u's':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 2);
                pattern += numberPattern[count];

                if (character == QLatin1Char('H'))
                {
                    components.push_back(Component::Hour);
                }
                else if (character == QLatin1Char('h'))
                {
                    components.push_back(Component::Hour12);
                }
                else if (character == QLatin1Char('M'))
                {
                    components.push_back(Component::Minute);
                }
                else
                {
                    components.push_back(Component::Second);
                }

                position += count;
            }
            break;

            case u't':
            {
                const qsizetype count = PDFFormFieldFormatHelper::getRepeatCount(format, position, 2);
                pattern += (count == 2) ? QStringLiteral("([ap]m)") : QStringLiteral("([ap])");
                components.push_back(Component::AmPm);
                position += count;
            }
            break;

            default:
                pattern += QRegularExpression::escape(QString(character));
                ++position;
                break;
        }
    }
    pattern += QStringLiteral("$");

    QRegularExpression expression(pattern, QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = expression.match(text.trimmed());
    if (!match.hasMatch())
    {
        return std::nullopt;
    }

    int year = QDate::currentDate().year();
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    bool isPM = false;
    bool isHour12 = false;

    for (size_t i = 0; i < components.size(); ++i)
    {
        const QString captured = match.captured(static_cast<int>(i + 1));
        const int value = captured.toInt();
        const Component component = components[i];

        // Ignored components (day names) are skipped
        if (component == Component::Year)
        {
            year = value;
        }
        else if (component == Component::ShortYear)
        {
            year = 2000 + value;
        }
        else if (component == Component::Month)
        {
            month = value;
        }
        else if (component == Component::MonthName)
        {
            // Regular expression accepts only month names, so the month is always found
            auto it = std::find_if(PDFFormFieldFormatHelper::MONTH_NAMES.cbegin(), PDFFormFieldFormatHelper::MONTH_NAMES.cend(), [&captured](const char* monthName) { return QString::fromLatin1(monthName).startsWith(captured, Qt::CaseInsensitive); });
            Q_ASSERT(it != PDFFormFieldFormatHelper::MONTH_NAMES.cend());
            month = static_cast<int>(std::distance(PDFFormFieldFormatHelper::MONTH_NAMES.cbegin(), it)) + 1;
        }
        else if (component == Component::Day)
        {
            day = value;
        }
        else if (component == Component::Hour)
        {
            hour = value;
        }
        else if (component == Component::Hour12)
        {
            hour = value;
            isHour12 = true;
        }
        else if (component == Component::Minute)
        {
            minute = value;
        }
        else if (component == Component::Second)
        {
            second = value;
        }
        else if (component == Component::AmPm)
        {
            isPM = captured.startsWith(QLatin1Char('p'), Qt::CaseInsensitive);
        }
    }

    if (isHour12)
    {
        if (hour < 1 || hour > 12)
        {
            return std::nullopt;
        }

        hour = hour % 12 + (isPM ? 12 : 0);
    }

    const QDate date(year, month, day);
    const QTime time(hour, minute, second);
    if (!date.isValid() || !time.isValid())
    {
        return std::nullopt;
    }

    return QDateTime(date, time);
}

std::optional<QDateTime> PDFFormFieldFormat::parseDate(const QString& format, const QString& text)
{
    if (std::optional<QDateTime> dateTime = scanDate(format, text))
    {
        return dateTime;
    }

    const QString trimmedText = text.trimmed();

    // Try ISO date or ISO date and time
    QDateTime isoDateTime = QDateTime::fromString(trimmedText, Qt::ISODate);
    if (isoDateTime.isValid())
    {
        return isoDateTime;
    }

    // Try to guess the date - numbers in the text are assigned to
    // the date components in the order given by the format.
    std::vector<QChar> order;
    for (qsizetype position = 0; position < format.size(); ++position)
    {
        QChar character = format[position];
        if (character == QLatin1Char('\\'))
        {
            ++position;
            continue;
        }

        if (character == QLatin1Char('d') && PDFFormFieldFormatHelper::getRepeatCount(format, position, 4) >= 3)
        {
            // Day name is not a date component
            position += PDFFormFieldFormatHelper::getRepeatCount(format, position, 4) - 1;
            continue;
        }

        if (character == QLatin1Char('h'))
        {
            character = QLatin1Char('H');
        }

        if (QStringView(u"ymdHMs").contains(character) && std::find(order.cbegin(), order.cend(), character) == order.cend())
        {
            order.push_back(character);
        }
    }

    static const QRegularExpression tokenExpression(QStringLiteral("(\\d+)|([^\\W\\d_]+)"));

    std::vector<int> numbers;
    std::optional<int> monthFromName;
    bool isPM = false;

    QRegularExpressionMatchIterator iterator = tokenExpression.globalMatch(trimmedText);
    while (iterator.hasNext())
    {
        QRegularExpressionMatch match = iterator.next();
        if (match.hasCaptured(1))
        {
            bool ok = false;
            const int number = match.captured(1).toInt(&ok);
            if (!ok)
            {
                return std::nullopt;
            }
            numbers.push_back(number);
        }
        else
        {
            const QString word = match.captured(2);
            if (word.compare(QLatin1String("pm"), Qt::CaseInsensitive) == 0 || word.compare(QLatin1String("p"), Qt::CaseInsensitive) == 0)
            {
                isPM = true;
            }
            else if (word.size() >= 3 && !monthFromName)
            {
                for (size_t monthIndex = 0; monthIndex < PDFFormFieldFormatHelper::MONTH_NAMES.size(); ++monthIndex)
                {
                    if (QString::fromLatin1(PDFFormFieldFormatHelper::MONTH_NAMES[monthIndex]).startsWith(word, Qt::CaseInsensitive))
                    {
                        monthFromName = static_cast<int>(monthIndex + 1);
                        break;
                    }
                }
            }
        }
    }

    if (numbers.empty())
    {
        return std::nullopt;
    }

    int year = QDate::currentDate().year();
    int month = monthFromName.value_or(1);
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;

    size_t numberIndex = 0;
    for (QChar component : order)
    {
        if ((component == QLatin1Char('m') && monthFromName) || numberIndex >= numbers.size())
        {
            continue;
        }

        const int number = numbers[numberIndex++];
        switch (component.unicode())
        {
            case u'y':
                year = (number < 100) ? 2000 + number : number;
                break;
            case u'm':
                month = number;
                break;
            case u'd':
                day = number;
                break;
            case u'H':
                hour = number;
                break;
            case u'M':
                minute = number;
                break;
            default:
                second = number;
                break;
        }
    }

    if (numberIndex < numbers.size())
    {
        // Text contains more numbers, than the format can use
        return std::nullopt;
    }

    if (isPM && hour < 12)
    {
        hour += 12;
    }

    const QDate date(year, month, day);
    const QTime time(hour, minute, second);
    if (!date.isValid() || !time.isValid())
    {
        return std::nullopt;
    }

    return QDateTime(date, time);
}

QString PDFFormFieldFormat::printMask(const QString& format, const QString& text)
{
    enum class Case
    {
        Preserve,
        Upper,
        Lower
    };

    Case currentCase = Case::Preserve;
    auto convert = [&currentCase](QChar character)
    {
        switch (currentCase)
        {
            case Case::Upper:
                return character.toUpper();
            case Case::Lower:
                return character.toLower();
            default:
                return character;
        }
    };

    auto copyNext = [&](qsizetype& position, const std::function<bool(QChar)>& predicate, QString& result)
    {
        while (position < text.size())
        {
            const QChar character = text[position++];
            if (predicate(character))
            {
                result += convert(character);
                break;
            }
        }
    };

    QString result;
    qsizetype position = 0;
    bool isEscaped = false;

    for (QChar command : format)
    {
        if (isEscaped)
        {
            result += command;
            isEscaped = false;
            continue;
        }

        if (position >= text.size())
        {
            break;
        }

        switch (command.unicode())
        {
            case u'?':
                result += convert(text[position++]);
                break;

            case u'X':
                copyNext(position, [](QChar character) { return PDFFormFieldFormatHelper::isAsciiDigit(character) || PDFFormFieldFormatHelper::isAsciiLetter(character); }, result);
                break;

            case u'A':
                copyNext(position, PDFFormFieldFormatHelper::isAsciiLetter, result);
                break;

            case u'9':
                copyNext(position, PDFFormFieldFormatHelper::isAsciiDigit, result);
                break;

            case u'*':
                while (position < text.size())
                {
                    result += convert(text[position++]);
                }
                break;

            case u'\\':
                isEscaped = true;
                break;

            case u'>':
                currentCase = Case::Upper;
                break;

            case u'<':
                currentCase = Case::Lower;
                break;

            case u'=':
                currentCase = Case::Preserve;
                break;

            default:
                result += command;
                break;
        }
    }

    return result;
}

}   // namespace pdf
