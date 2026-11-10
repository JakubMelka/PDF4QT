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

#include "pdffilenametemplate.h"

#include <QFileInfo>
#include <QSet>

namespace pdf
{

/// Helpers of the template of the names
class PDFFileNameTemplateHelper
{
public:
    PDFFileNameTemplateHelper() = delete;

    /// The named tokens of K25 in braces
    static constexpr const char* TOKEN_SOURCE_BASE = "{source_base}";
    static constexpr const char* TOKEN_SOURCE_NAME = "{source_name}";
    static constexpr const char* TOKEN_SOURCE_EXT = "{source_ext}";
    static constexpr const char* TOKEN_SOURCE_PAGE = "{source_page}";
    static constexpr const char* TOKEN_OUTPUT_INDEX = "{output_index}";
    static constexpr const char* TOKEN_GROUP_INDEX = "{group_index}";
    static constexpr const char* TOKEN_GROUP_NAME = "{group_name}";
    static constexpr const char* TOKEN_DATE = "{date}";

    /// A character, which is never a part of a name: the control characters and the
    /// bidirectional controls (they hide the extension of a name)
    static bool isRemovedCharacter(char32_t character)
    {
        if (character < 0x20 || (character >= 0x7F && character <= 0x9F))
        {
            return true;
        }
        if (character == 0x061C || character == 0x200E || character == 0x200F)
        {
            return true;
        }
        return (character >= 0x202A && character <= 0x202E) || (character >= 0x2066 && character <= 0x2069);
    }

    /// A character, which a file name of Windows, macOS or Linux cannot have (a separator of
    /// folders, a stream of NTFS, the reserved characters of Windows): it becomes '_'
    static bool isReplacedCharacter(char32_t character)
    {
        switch (character)
        {
            case U'/':
            case U'\\':
            case U':':
            case U'<':
            case U'>':
            case U'"':
            case U'|':
            case U'?':
            case U'*':
                return true;

            default:
                return false;
        }
    }

    /// Number of the decimal digits of a positive number
    static int getDigitCount(qint64 value)
    {
        int digits = 1;
        while (value >= 10)
        {
            value /= 10;
            ++digits;
        }
        return digits;
    }

    /// A number of a run of a token character: a single character is the plain number, a run of
    /// more is padded with zeros to the width of the run (or of \p minimumWidth, when wider)
    static QString formatNumber(qint64 value, qsizetype runLength, int minimumWidth)
    {
        const QString text = QString::number(qMax(qint64(0), value));
        if (runLength < 2)
        {
            return text;
        }
        return text.rightJustified(qMax(int(runLength), minimumWidth), QLatin1Char('0'));
    }

    /// The text cut to at most \p length UTF-16 units, never inside of a surrogate pair
    static QString truncate(const QString& text, int length)
    {
        if (text.size() <= length)
        {
            return text;
        }
        qsizetype end = qMax(0, length);
        if (end > 0 && text.at(end - 1).isHighSurrogate())
        {
            --end;
        }
        return text.left(end);
    }

    /// sanitize with the longest name \p maximumLength
    static QString sanitize(const QString& name, int maximumLength)
    {
        QString result;
        result.reserve(name.size());
        for (const char32_t character : name.toUcs4())
        {
            if (isRemovedCharacter(character))
            {
                continue;
            }
            if (isReplacedCharacter(character))
            {
                result += QLatin1Char('_');
                continue;
            }
            if (character == U'.' && result.endsWith(QLatin1Char('.')))
            {
                // ".." is never a part of a name (a way to the parent folder)
                continue;
            }
            result += QString::fromUcs4(&character, 1);
        }

        // Windows removes a trailing dot or space, so another file would be written
        result = result.trimmed();
        while (result.endsWith(QLatin1Char('.')) || result.endsWith(QLatin1Char(' ')))
        {
            result.chop(1);
        }
        if (result.isEmpty())
        {
            result = QStringLiteral("_");
        }
        if (PDFFileNameTemplate::isReservedDeviceName(result))
        {
            result.prepend(QLatin1Char('_'));
        }

        result = truncate(result, qMax(1, maximumLength));
        while (result.size() > 1 && (result.endsWith(QLatin1Char('.')) || result.endsWith(QLatin1Char(' '))))
        {
            result.chop(1);
        }
        return result;
    }

    /// The text of a named token at \p position of \p pattern, its length in \p length; a null
    /// text, when there is no named token
    static QString readNamedToken(const QString& pattern, qsizetype position, const PDFFileNameTemplate::Values& values, qsizetype* length)
    {
        const QFileInfo sourceInfo(values.sourceFileName);
        const std::pair<const char*, QString> tokens[] = {
            { TOKEN_SOURCE_BASE, sourceInfo.completeBaseName() },
            { TOKEN_SOURCE_NAME, values.sourceFileName },
            { TOKEN_SOURCE_EXT, sourceInfo.suffix() },
            { TOKEN_SOURCE_PAGE, QString::number(values.pageNumber) },
            { TOKEN_OUTPUT_INDEX, QString::number(values.outputIndex) },
            { TOKEN_GROUP_INDEX, QString::number(values.groupIndex) },
            { TOKEN_GROUP_NAME, values.groupName },
            { TOKEN_DATE, (values.date.isValid() ? values.date : QDate::currentDate()).toString(Qt::ISODate) },
        };

        for (const auto& [token, value] : tokens)
        {
            const QLatin1String tokenText(token);
            if (QStringView(pattern).mid(position).startsWith(tokenText))
            {
                *length = tokenText.size();
                return value.isNull() ? QString(QLatin1String("")) : value;
            }
        }
        return QString();
    }
};

PDFFileNameTemplate::PDFFileNameTemplate(QString pattern, QString extension) :
    m_pattern(std::move(pattern)),
    m_extension(std::move(extension))
{

}

bool PDFFileNameTemplate::isValid(QString* errorMessage) const
{
    using Helper = PDFFileNameTemplateHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (m_pattern.trimmed().isEmpty())
    {
        error = tr("Enter a name of the files.");
        return false;
    }
    if (m_pattern.size() + m_extension.size() > MAXIMUM_FILE_NAME_LENGTH)
    {
        error = tr("The name of the files is too long.");
        return false;
    }

    for (const char32_t character : m_pattern.toUcs4())
    {
        if (character == U'/' || character == U'\\')
        {
            error = tr("The name of the files cannot contain a folder. Choose the folder in Folder.");
            return false;
        }
        if (Helper::isRemovedCharacter(character) || Helper::isReplacedCharacter(character))
        {
            error = tr("The name of the files contains a character, which cannot be used in a file name.");
            return false;
        }
    }
    return true;
}

QString PDFFileNameTemplate::createFileName(const Values& values) const
{
    using Helper = PDFFileNameTemplateHelper;

    // Every occurrence of every token is replaced (B25: "part-#-of-#" keeps "-of-")
    const int outputWidth = Helper::getDigitCount(qMax(values.outputCount, values.outputIndex));
    QString expanded;
    qsizetype position = 0;
    while (position < m_pattern.size())
    {
        const QChar character = m_pattern.at(position);
        if (character == QLatin1Char('{'))
        {
            qsizetype length = 0;
            const QString value = Helper::readNamedToken(m_pattern, position, values, &length);
            if (!value.isNull())
            {
                expanded += value;
                position += length;
                continue;
            }
        }

        if (character == QLatin1Char('#') || character == QLatin1Char('@') || character == QLatin1Char('%'))
        {
            qsizetype end = position;
            while (end < m_pattern.size() && m_pattern.at(end) == character)
            {
                ++end;
            }
            const qsizetype runLength = end - position;
            if (character == QLatin1Char('#'))
            {
                expanded += Helper::formatNumber(values.outputIndex, runLength, outputWidth);
            }
            else if (character == QLatin1Char('@'))
            {
                expanded += Helper::formatNumber(values.pageNumber, runLength, 0);
            }
            else
            {
                expanded += Helper::formatNumber(values.sourceIndex, runLength, 0);
            }
            position = end;
            continue;
        }

        expanded += character;
        ++position;
    }

    return Helper::sanitize(expanded, MAXIMUM_FILE_NAME_LENGTH - int(m_extension.size())) + m_extension;
}

std::vector<int> PDFFileNameTemplate::findDuplicates(const QStringList& fileNames)
{
    std::vector<int> duplicates;
    QSet<QString> names;
    names.reserve(fileNames.size());
    for (qsizetype i = 0; i < fileNames.size(); ++i)
    {
        const QString key = fileNames[i].toCaseFolded();
        if (names.contains(key))
        {
            duplicates.push_back(int(i));
        }
        else
        {
            names.insert(key);
        }
    }
    return duplicates;
}

QString PDFFileNameTemplate::sanitize(const QString& name)
{
    return PDFFileNameTemplateHelper::sanitize(name, MAXIMUM_FILE_NAME_LENGTH);
}

bool PDFFileNameTemplate::isReservedDeviceName(const QString& name)
{
    const QString base = name.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
    static const QStringList names = { QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
                                       QStringLiteral("CONIN$"), QStringLiteral("CONOUT$") };
    if (names.contains(base))
    {
        return true;
    }

    // COM1-9, LPT1-9 (also the superscript digits Windows accepts)
    if (base.size() == 4 && (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT"))))
    {
        const QChar digit = base[3];
        return (digit >= u'1' && digit <= u'9') || digit == QChar(0x00B9) || digit == QChar(0x00B2) || digit == QChar(0x00B3);
    }
    return false;
}

}   // namespace pdf
