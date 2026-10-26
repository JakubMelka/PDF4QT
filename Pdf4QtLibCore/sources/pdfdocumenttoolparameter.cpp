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

#include "pdfdocumenttoolparameter.h"
#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfutils.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pdf
{

/// Helpers of the parameters of the tools. The texts are translated in the
/// context PDFDocumentToolParameterDefinition::TRANSLATION_CONTEXT - lupdate
/// takes the context of the macro, so the calls are qualified by the name of
/// this class (lupdate does not resolve the local alias Helper).
class PDFDocumentToolParameterHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolParameterDefinition)

public:
    PDFDocumentToolParameterHelper() = delete;

    /// Longest stored JSON text of a page selection
    static constexpr int MAXIMUM_STORED_JSON_LENGTH = 16384;

    /// Most unknown keys reported by one validation
    static constexpr int MAXIMUM_UNKNOWN_KEY_ERRORS = 16;

    /// The value is missing: an invalid variant or a null of JSON
    static bool isMissing(const QVariant& value)
    {
        return !value.isValid() || value.typeId() == QMetaType::Nullptr;
    }

    /// Integer of an untrusted variant: integer types, a double with an integral
    /// value in the safe range of JSON (bool, text and anything else is refused)
    static bool readInteger(const QVariant& value, qint64* result)
    {
        switch (value.typeId())
        {
            case QMetaType::Int:
            case QMetaType::Short:
            case QMetaType::Long:
            case QMetaType::LongLong:
                *result = value.toLongLong();
                return true;

            case QMetaType::UInt:
            case QMetaType::UShort:
            case QMetaType::ULong:
            case QMetaType::ULongLong:
            {
                const qulonglong number = value.toULongLong();
                if (number > qulonglong(std::numeric_limits<qint64>::max()))
                {
                    return false;
                }
                *result = qint64(number);
                return true;
            }

            case QMetaType::Double:
            case QMetaType::Float:
            {
                const double number = value.toDouble();
                if (!std::isfinite(number) || std::trunc(number) != number || std::abs(number) > double(PDFDocumentToolParameterDefinition::MAXIMUM_SAFE_INTEGER))
                {
                    return false;
                }
                *result = qint64(number);
                return true;
            }

            default:
                return false;
        }
    }

    /// Finite number of an untrusted variant (integer and floating point types)
    static bool readReal(const QVariant& value, double* result)
    {
        switch (value.typeId())
        {
            case QMetaType::Int:
            case QMetaType::Short:
            case QMetaType::Long:
            case QMetaType::LongLong:
            case QMetaType::UInt:
            case QMetaType::UShort:
            case QMetaType::ULong:
            case QMetaType::ULongLong:
            case QMetaType::Double:
            case QMetaType::Float:
                *result = value.toDouble();
                return std::isfinite(*result);

            default:
                return false;
        }
    }

    /// The text has a lone surrogate (invalid UTF-16 of a hostile JSON)
    static bool hasLoneSurrogate(const QString& text)
    {
        for (qsizetype i = 0; i < text.size(); ++i)
        {
            const QChar character = text[i];
            if (character.isHighSurrogate())
            {
                if (i + 1 >= text.size() || !text[i + 1].isLowSurrogate())
                {
                    return true;
                }
                ++i;
            }
            else if (character.isLowSurrogate())
            {
                return true;
            }
        }
        return false;
    }

    /// The text has a control character (C0, DEL, C1)
    static bool hasControlCharacter(const QString& text)
    {
        for (const QChar character : text)
        {
            if (character.category() == QChar::Other_Control)
            {
                return true;
            }
        }
        return false;
    }

    /// The text has a bidirectional embedding, override or isolate (U+202A to
    /// U+202E, U+2066 to U+2069): a path or a file name shown with it is spoofed
    /// ("report‮fdp.exe"); the marks U+200E / U+200F stay (texts of right-to-left scripts)
    static bool hasBidirectionalControl(const QString& text)
    {
        for (const QChar character : text)
        {
            const char16_t code = character.unicode();
            if ((code >= 0x202A && code <= 0x202E) || (code >= 0x2066 && code <= 0x2069))
            {
                return true;
            }
        }
        return false;
    }

    /// The path lies in a device namespace of Windows (\\.\PhysicalDrive0,
    /// \\?\GLOBALROOT\..., \??\..., any separators): a disk, a pipe, a device -
    /// never a file or a folder of the user (review RS6)
    static bool isDevicePath(const QString& text)
    {
        QString prefix = text.left(4);
        prefix.replace(QLatin1Char('\\'), QLatin1Char('/'));
        // ("/?" "?/" is split, a trigraph would be read in it)
        return prefix == QLatin1String("//./") || prefix == QLatin1String("//?/") || prefix == QLatin1String("/?" "?/");
    }

    /// The file name of a template leaves its folder: a separator, a drive or a
    /// stream (':' - QDir::filePath returns "C:name" itself, Windows writes it into
    /// the current folder of the drive C:) or a name of dots and spaces only ("..")
    static bool isFileNameOutsideOfFolder(const QString& text)
    {
        if (text.contains(QLatin1Char('/')) || text.contains(QLatin1Char('\\')) || text.contains(QLatin1Char(':')))
        {
            return true;
        }

        for (const QChar character : text)
        {
            if (character != QLatin1Char('.') && character != QLatin1Char(' '))
            {
                return false;
            }
        }
        return !text.isEmpty();
    }

    /// Characters of a page range text: digits, '-', ',' and spaces; every number
    /// has at most 10 digits (no overflow of the parser), every part between the
    /// commas at most one '-' (the parser would read "-5-" as all pages)
    static bool isPageRangeText(const QString& text)
    {
        int digits = 0;
        int dashes = 0;
        for (const QChar character : text)
        {
            if (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
            {
                if (++digits > 10)
                {
                    return false;
                }
                continue;
            }

            digits = 0;
            if (character == QLatin1Char('-'))
            {
                if (++dashes > 1)
                {
                    return false;
                }
            }
            else if (character == QLatin1Char(','))
            {
                dashes = 0;
            }
            else if (character != QLatin1Char(' ') && character != QLatin1Char('\t'))
            {
                return false;
            }
        }
        return true;
    }

    /// Parses the range of pages 1 to \p last; the set is empty on an error or when
    /// a page lies outside of 1 to \p last
    static PDFClosedIntervalSet parsePages(const QString& pages, PDFInteger last, bool* isValid)
    {
        *isValid = false;
        if (last < 1 || !isPageRangeText(pages))
        {
            return PDFClosedIntervalSet();
        }

        QString errorMessage;
        const QString text = pages.trimmed().isEmpty() ? QStringLiteral("-") : pages;
        PDFClosedIntervalSet set = PDFClosedIntervalSet::parse(1, last, text, &errorMessage);
        if (!errorMessage.isEmpty() || set.isEmpty())
        {
            return PDFClosedIntervalSet();
        }

        // Every page between 1 and last: the union with [1, last] is [1, last] itself.
        // The sets are compared, not their lengths - getTotalLength sums in an int,
        // so a range longer by a multiple of 2^32 had the length of the pages and
        // unfold wrote behind its vector (review RS6, SECURITY_TODO 12)
        PDFClosedIntervalSet bounds;
        bounds.addInterval(1, last);
        PDFClosedIntervalSet merged = bounds;
        merged.merge(set);
        if (merged != bounds)
        {
            return PDFClosedIntervalSet();
        }

        *isValid = true;
        return set;
    }

    /// Filter of its name, None for an unknown one
    static PDFDocumentToolPageSelection::Filter getFilter(const QString& name)
    {
        const QStringList names = PDFDocumentToolPageSelection::getFilterNames();
        const PDFDocumentToolPageSelection::Filter filters[] = { PDFDocumentToolPageSelection::Filter::Even, PDFDocumentToolPageSelection::Filter::Odd,
                                                                 PDFDocumentToolPageSelection::Filter::Portrait, PDFDocumentToolPageSelection::Filter::Landscape };
        for (qsizetype i = 0; i < names.size(); ++i)
        {
            if (names[i] == name)
            {
                return filters[i];
            }
        }
        return PDFDocumentToolPageSelection::Filter::None;
    }

    /// Text of a color "#rrggbb"
    static bool isColorText(const QString& text)
    {
        if (text.size() != 7 || text[0] != QLatin1Char('#'))
        {
            return false;
        }
        for (qsizetype i = 1; i < text.size(); ++i)
        {
            const QChar character = text[i];
            const bool isHex = (character >= QLatin1Char('0') && character <= QLatin1Char('9')) ||
                               (character >= QLatin1Char('a') && character <= QLatin1Char('f')) ||
                               (character >= QLatin1Char('A') && character <= QLatin1Char('F'));
            if (!isHex)
            {
                return false;
            }
        }
        return true;
    }

    static bool isTextType(PDFDocumentToolParameterType type)
    {
        switch (type)
        {
            case PDFDocumentToolParameterType::String:
            case PDFDocumentToolParameterType::FilePath:
            case PDFDocumentToolParameterType::FolderPath:
            case PDFDocumentToolParameterType::FileNameTemplate:
            case PDFDocumentToolParameterType::Secret:
                return true;

            default:
                return false;
        }
    }

    static QString getTypeName(PDFDocumentToolParameterType type)
    {
        switch (type)
        {
            case PDFDocumentToolParameterType::Bool:
                return QStringLiteral("boolean");
            case PDFDocumentToolParameterType::Integer:
                return QStringLiteral("integer");
            case PDFDocumentToolParameterType::Real:
                return QStringLiteral("number");
            case PDFDocumentToolParameterType::PageSelection:
                return QStringLiteral("object");
            default:
                return QStringLiteral("string");
        }
    }
};

QVariant PDFDocumentToolPageSelection::toVariant() const
{
    QStringList filterNames;
    const QStringList names = getFilterNames();
    const Filter values[] = { Filter::Even, Filter::Odd, Filter::Portrait, Filter::Landscape };
    for (qsizetype i = 0; i < names.size(); ++i)
    {
        if (filters.testFlag(values[i]))
        {
            filterNames << names[i];
        }
    }

    QVariantMap map;
    map.insert(QStringLiteral("pages"), pages);
    map.insert(QStringLiteral("filters"), filterNames);
    return map;
}

std::optional<PDFDocumentToolPageSelection> PDFDocumentToolPageSelection::fromVariant(const QVariant& value, QString* errorMessage)
{
    using Helper = PDFDocumentToolParameterHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    PDFDocumentToolPageSelection selection;
    if (value.typeId() == QMetaType::QString)
    {
        selection.pages = value.toString();
    }
    else if (value.typeId() == QMetaType::QVariantMap)
    {
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
        {
            if (it.key() != QLatin1String("pages") && it.key() != QLatin1String("filters"))
            {
                error = PDFDocumentToolParameterHelper::tr("Unknown key “%1” of the page selection.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(it.key()));
                return std::nullopt;
            }
        }

        const QVariant pages = map.value(QStringLiteral("pages"));
        if (Helper::isMissing(pages))
        {
            selection.pages.clear();
        }
        else if (pages.typeId() == QMetaType::QString)
        {
            selection.pages = pages.toString();
        }
        else
        {
            error = PDFDocumentToolParameterHelper::tr("The pages must be a text such as 1-3, 5, 8-.");
            return std::nullopt;
        }

        const QVariant filters = map.value(QStringLiteral("filters"));
        if (!Helper::isMissing(filters))
        {
            if (filters.typeId() != QMetaType::QVariantList && filters.typeId() != QMetaType::QStringList)
            {
                error = PDFDocumentToolParameterHelper::tr("The page filters must be a list of even, odd, portrait and landscape.");
                return std::nullopt;
            }

            const QVariantList list = filters.toList();
            if (list.size() > getFilterNames().size())
            {
                error = PDFDocumentToolParameterHelper::tr("The page filters must be a list of even, odd, portrait and landscape.");
                return std::nullopt;
            }

            for (const QVariant& item : list)
            {
                const Filter filter = item.typeId() == QMetaType::QString ? Helper::getFilter(item.toString()) : Filter::None;
                if (filter == Filter::None)
                {
                    error = PDFDocumentToolParameterHelper::tr("The page filters must be a list of even, odd, portrait and landscape.");
                    return std::nullopt;
                }
                selection.filters |= filter;
            }
        }
    }
    else
    {
        error = PDFDocumentToolParameterHelper::tr("The pages must be a text such as 1-3, 5, 8-.");
        return std::nullopt;
    }

    if (selection.pages.size() > MAXIMUM_PAGES_LENGTH)
    {
        error = PDFDocumentToolParameterHelper::tr("The page range is longer than %1 characters.").arg(MAXIMUM_PAGES_LENGTH);
        return std::nullopt;
    }

    bool isValid = false;
    Helper::parsePages(selection.pages, MAXIMUM_PAGE_NUMBER, &isValid);
    if (!isValid)
    {
        error = PDFDocumentToolParameterHelper::tr("“%1” is not a valid page range. Write pages from 1 such as 1-3, 5, 8-.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(selection.pages));
        return std::nullopt;
    }

    selection.pages = selection.pages.trimmed();
    return selection;
}

std::vector<PDFInteger> PDFDocumentToolPageSelection::getPageIndices(const std::vector<QSizeF>& pageSizes, QString* errorMessage) const
{
    using Helper = PDFDocumentToolParameterHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    std::vector<PDFInteger> indices;
    const PDFInteger pageCount = PDFInteger(pageSizes.size());
    if (pageCount == 0)
    {
        error = PDFDocumentToolParameterHelper::tr("The document has no pages.");
        return indices;
    }

    if (pages.size() > MAXIMUM_PAGES_LENGTH)
    {
        error = PDFDocumentToolParameterHelper::tr("The page range is longer than %1 characters.").arg(MAXIMUM_PAGES_LENGTH);
        return indices;
    }

    bool isValid = false;
    const PDFClosedIntervalSet set = Helper::parsePages(pages, pageCount, &isValid);
    if (!isValid)
    {
        error = PDFDocumentToolParameterHelper::tr("“%1” is not a valid range of the pages 1 to %2.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(pages), QString::number(pageCount));
        return indices;
    }

    const bool even = filters.testFlag(Filter::Even);
    const bool odd = filters.testFlag(Filter::Odd);
    const bool portrait = filters.testFlag(Filter::Portrait);
    const bool landscape = filters.testFlag(Filter::Landscape);

    for (const PDFInteger pageNumber : set.unfold())
    {
        if (even != odd && ((pageNumber % 2 == 0) != even))
        {
            continue;
        }

        const QSizeF size = pageSizes[size_t(pageNumber - 1)];
        const bool isLandscape = size.width() > size.height();
        if (portrait != landscape && isLandscape != landscape)
        {
            continue;
        }

        indices.push_back(pageNumber - 1);
    }

    if (indices.empty())
    {
        error = PDFDocumentToolParameterHelper::tr("No page matches the page selection.");
    }
    return indices;
}

std::vector<PDFInteger> PDFDocumentToolPageSelection::getPageIndices(const PDFDocument* document, QString* errorMessage) const
{
    std::vector<QSizeF> pageSizes;
    if (document)
    {
        const PDFCatalog* catalog = document->getCatalog();
        const size_t pageCount = catalog->getPageCount();
        pageSizes.reserve(pageCount);
        for (size_t i = 0; i < pageCount; ++i)
        {
            pageSizes.push_back(catalog->getPage(i)->getRotatedCropBox().size());
        }
    }
    return getPageIndices(pageSizes, errorMessage);
}

QStringList PDFDocumentToolPageSelection::getFilterNames()
{
    return { QStringLiteral("even"), QStringLiteral("odd"), QStringLiteral("portrait"), QStringLiteral("landscape") };
}

PDFDocumentToolParameterDefinition::PDFDocumentToolParameterDefinition(const QString& name, PDFDocumentToolParameterType type, const QString& title, const QString& description) :
    m_name(name),
    m_type(type),
    m_title(title),
    m_description(description)
{
    m_maximumLength = getTypeMaximumLength();
}

PDFDocumentToolParameterDefinition PDFDocumentToolParameterDefinition::createBool(const QString& name, const QString& title, const QString& description, bool defaultValue)
{
    PDFDocumentToolParameterDefinition definition(name, PDFDocumentToolParameterType::Bool, title, description);
    definition.m_defaultValue = defaultValue;
    return definition;
}

PDFDocumentToolParameterDefinition PDFDocumentToolParameterDefinition::createInteger(const QString& name, const QString& title, const QString& description, qint64 defaultValue, qint64 minimum, qint64 maximum)
{
    PDFDocumentToolParameterDefinition definition(name, PDFDocumentToolParameterType::Integer, title, description);
    definition.m_defaultValue = defaultValue;
    definition.m_integerMinimum = minimum;
    definition.m_integerMaximum = maximum;
    return definition;
}

PDFDocumentToolParameterDefinition PDFDocumentToolParameterDefinition::createReal(const QString& name, const QString& title, const QString& description, double defaultValue, double minimum, double maximum)
{
    PDFDocumentToolParameterDefinition definition(name, PDFDocumentToolParameterType::Real, title, description);
    definition.m_defaultValue = defaultValue;
    definition.m_realMinimum = minimum;
    definition.m_realMaximum = maximum;
    return definition;
}

PDFDocumentToolParameterDefinition PDFDocumentToolParameterDefinition::createString(const QString& name, const QString& title, const QString& description, const QString& defaultValue, int maximumLength)
{
    PDFDocumentToolParameterDefinition definition(name, PDFDocumentToolParameterType::String, title, description);
    definition.m_defaultValue = defaultValue;
    definition.setMaximumLength(maximumLength);
    return definition;
}

PDFDocumentToolParameterDefinition PDFDocumentToolParameterDefinition::createEnum(const QString& name, const QString& title, const QString& description, std::vector<PDFDocumentToolParameterOption> options, const QString& defaultId)
{
    PDFDocumentToolParameterDefinition definition(name, PDFDocumentToolParameterType::Enum, title, description);
    definition.m_defaultValue = defaultId;
    definition.m_options = std::move(options);
    return definition;
}

PDFDocumentToolParameterDefinition PDFDocumentToolParameterDefinition::createPageSelection(const QString& name, const QString& title, const QString& description, const PDFDocumentToolPageSelection& defaultValue)
{
    PDFDocumentToolParameterDefinition definition(name, PDFDocumentToolParameterType::PageSelection, title, description);
    definition.m_defaultValue = defaultValue.toVariant();
    return definition;
}

PDFDocumentToolParameterDefinition PDFDocumentToolParameterDefinition::createColor(const QString& name, const QString& title, const QString& description, const QColor& defaultValue)
{
    PDFDocumentToolParameterDefinition definition(name, PDFDocumentToolParameterType::Color, title, description);
    definition.m_defaultValue = defaultValue.isValid() ? QVariant(QColor::fromRgb(defaultValue.rgb())) : QVariant(defaultValue);
    return definition;
}

PDFDocumentToolParameterDefinition PDFDocumentToolParameterDefinition::createFilePath(const QString& name, const QString& title, const QString& description, const QString& defaultValue)
{
    PDFDocumentToolParameterDefinition definition(name, PDFDocumentToolParameterType::FilePath, title, description);
    definition.m_defaultValue = defaultValue;
    return definition;
}

PDFDocumentToolParameterDefinition PDFDocumentToolParameterDefinition::createFolderPath(const QString& name, const QString& title, const QString& description, const QString& defaultValue)
{
    PDFDocumentToolParameterDefinition definition(name, PDFDocumentToolParameterType::FolderPath, title, description);
    definition.m_defaultValue = defaultValue;
    return definition;
}

PDFDocumentToolParameterDefinition PDFDocumentToolParameterDefinition::createFileNameTemplate(const QString& name, const QString& title, const QString& description, const QString& defaultValue)
{
    PDFDocumentToolParameterDefinition definition(name, PDFDocumentToolParameterType::FileNameTemplate, title, description);
    definition.m_defaultValue = defaultValue;
    return definition;
}

PDFDocumentToolParameterDefinition PDFDocumentToolParameterDefinition::createSecret(const QString& name, const QString& title, const QString& description)
{
    PDFDocumentToolParameterDefinition definition(name, PDFDocumentToolParameterType::Secret, title, description);
    definition.m_defaultValue = QString();
    return definition;
}

PDFDocumentToolParameterDefinition& PDFDocumentToolParameterDefinition::setMaximumLength(int maximumLength)
{
    if (PDFDocumentToolParameterHelper::isTextType(m_type))
    {
        m_maximumLength = std::clamp(maximumLength, 1, getTypeMaximumLength());
    }
    return *this;
}

int PDFDocumentToolParameterDefinition::getTypeMaximumLength() const
{
    switch (m_type)
    {
        case PDFDocumentToolParameterType::String:
        case PDFDocumentToolParameterType::FileNameTemplate:
            return MAXIMUM_TEXT_LENGTH;
        case PDFDocumentToolParameterType::FilePath:
        case PDFDocumentToolParameterType::FolderPath:
            return MAXIMUM_PATH_LENGTH;
        case PDFDocumentToolParameterType::Secret:
            return MAXIMUM_SECRET_LENGTH;
        default:
            return 0;
    }
}

bool PDFDocumentToolParameterDefinition::isValidName(const QString& name)
{
    if (name.isEmpty() || name.size() > MAXIMUM_NAME_LENGTH)
    {
        return false;
    }

    for (qsizetype i = 0; i < name.size(); ++i)
    {
        const char16_t character = name[i].unicode();
        const bool isLetter = (character >= u'a' && character <= u'z') || (character >= u'A' && character <= u'Z');
        const bool isDigit = character >= u'0' && character <= u'9';
        if (!isLetter && (i == 0 || (!isDigit && character != u'_')))
        {
            return false;
        }
    }
    return true;
}

bool PDFDocumentToolParameterDefinition::isValidOptionId(const QString& id)
{
    if (id.isEmpty() || id.size() > MAXIMUM_NAME_LENGTH)
    {
        return false;
    }

    for (qsizetype i = 0; i < id.size(); ++i)
    {
        const char16_t character = id[i].unicode();
        const bool isLetterOrDigit = (character >= u'a' && character <= u'z') || (character >= u'0' && character <= u'9');
        const bool isSeparator = character == u'-' || character == u'_';
        if (!isLetterOrDigit && (!isSeparator || i == 0 || i == id.size() - 1))
        {
            return false;
        }
    }
    return true;
}

QString PDFDocumentToolParameterDefinition::sanitizeForMessage(const QString& text, int maximumLength)
{
    QString result;
    const int limit = std::max(maximumLength, 1);
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const QChar character = text[i];
        const QChar::Category category = character.category();
        if (category == QChar::Other_Control || category == QChar::Other_Format || category == QChar::Other_Surrogate ||
            category == QChar::Separator_Line || category == QChar::Separator_Paragraph)
        {
            continue;
        }

        if (result.size() >= limit)
        {
            result.chop(1);
            result += QChar(0x2026);
            break;
        }
        result += character;
    }
    return result;
}

QString PDFDocumentToolParameterDefinition::getLabel() const
{
    return m_title.isEmpty() ? m_name : sanitizeForMessage(m_title, MAXIMUM_NAME_LENGTH);
}

bool PDFDocumentToolParameterDefinition::isValid(QString* errorMessage) const
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (!isValidName(m_name))
    {
        error = QStringLiteral("Invalid parameter name '%1'.").arg(sanitizeForMessage(m_name));
        return false;
    }

    switch (m_type)
    {
        case PDFDocumentToolParameterType::Integer:
            if (m_integerMinimum > m_integerMaximum)
            {
                error = QStringLiteral("Parameter '%1': the minimum is greater than the maximum.").arg(m_name);
                return false;
            }
            break;

        case PDFDocumentToolParameterType::Real:
            if (!std::isfinite(m_realMinimum) || !std::isfinite(m_realMaximum) || m_realMinimum > m_realMaximum)
            {
                error = QStringLiteral("Parameter '%1': invalid range.").arg(m_name);
                return false;
            }
            break;

        case PDFDocumentToolParameterType::Enum:
        {
            if (m_options.empty() || int(m_options.size()) > MAXIMUM_OPTION_COUNT)
            {
                error = QStringLiteral("Parameter '%1': an enumeration needs 1 to %2 options.").arg(m_name).arg(MAXIMUM_OPTION_COUNT);
                return false;
            }

            QStringList ids;
            for (const PDFDocumentToolParameterOption& option : m_options)
            {
                if (!isValidOptionId(option.id) || ids.contains(option.id))
                {
                    error = QStringLiteral("Parameter '%1': invalid or duplicate option '%2'.").arg(m_name, sanitizeForMessage(option.id));
                    return false;
                }
                ids << option.id;
            }
            break;
        }

        default:
            // A Secret has no default (createSecret), the maximum length of a text
            // type is bounded by setMaximumLength
            break;
    }

    // The default of an optional parameter must be a valid value
    if (!m_required)
    {
        QVariant normalized;
        const QString defaultError = normalize(m_defaultValue, &normalized);
        if (!defaultError.isEmpty() || normalized != m_defaultValue)
        {
            error = QStringLiteral("Parameter '%1': invalid default value.").arg(m_name);
            return false;
        }
    }

    return true;
}

QString PDFDocumentToolParameterDefinition::normalizeText(const QVariant& value, QVariant* normalizedValue) const
{
    using Helper = PDFDocumentToolParameterHelper;

    if (value.typeId() != QMetaType::QString)
    {
        return PDFDocumentToolParameterHelper::tr("%1: the value must be a text.").arg(getLabel());
    }

    const QString text = value.toString();
    if (text.size() > m_maximumLength)
    {
        return PDFDocumentToolParameterHelper::tr("%1: the text is longer than %2 characters.").arg(getLabel()).arg(m_maximumLength);
    }

    if (text.isEmpty() && m_required)
    {
        return PDFDocumentToolParameterHelper::tr("%1: a value is required.").arg(getLabel());
    }

    bool isForbidden = Helper::hasLoneSurrogate(text) || text.contains(QChar(0));
    switch (m_type)
    {
        case PDFDocumentToolParameterType::FilePath:
        case PDFDocumentToolParameterType::FolderPath:
            isForbidden = isForbidden || Helper::hasControlCharacter(text) || Helper::hasBidirectionalControl(text);
            if (!isForbidden && Helper::isDevicePath(text))
            {
                return PDFDocumentToolParameterHelper::tr("%1: a device path cannot be used.").arg(getLabel());
            }
            break;

        case PDFDocumentToolParameterType::FileNameTemplate:
            isForbidden = isForbidden || Helper::hasControlCharacter(text) || Helper::hasBidirectionalControl(text);
            if (!isForbidden && Helper::isFileNameOutsideOfFolder(text))
            {
                return PDFDocumentToolParameterHelper::tr("%1: a file name cannot contain a folder.").arg(getLabel());
            }
            break;

        default:
            break;
    }

    if (isForbidden)
    {
        return PDFDocumentToolParameterHelper::tr("%1: the text contains characters, which are not allowed.").arg(getLabel());
    }

    *normalizedValue = text;
    return QString();
}

QString PDFDocumentToolParameterDefinition::normalize(const QVariant& value, QVariant* normalizedValue) const
{
    using Helper = PDFDocumentToolParameterHelper;

    QVariant dummy;
    QVariant& result = normalizedValue ? *normalizedValue : dummy;
    result = QVariant();

    if (Helper::isMissing(value))
    {
        if (m_required)
        {
            return PDFDocumentToolParameterHelper::tr("%1: a value is required.").arg(getLabel());
        }
        result = m_defaultValue;
        return QString();
    }

    switch (m_type)
    {
        case PDFDocumentToolParameterType::Bool:
            if (value.typeId() != QMetaType::Bool)
            {
                return PDFDocumentToolParameterHelper::tr("%1: the value must be true or false.").arg(getLabel());
            }
            result = value.toBool();
            return QString();

        case PDFDocumentToolParameterType::Integer:
        {
            qint64 number = 0;
            if (!Helper::readInteger(value, &number))
            {
                return PDFDocumentToolParameterHelper::tr("%1: the value must be a whole number.").arg(getLabel());
            }
            if (number < m_integerMinimum || number > m_integerMaximum)
            {
                return PDFDocumentToolParameterHelper::tr("%1: the value must be between %2 and %3.").arg(getLabel()).arg(m_integerMinimum).arg(m_integerMaximum);
            }
            result = number;
            return QString();
        }

        case PDFDocumentToolParameterType::Real:
        {
            double number = 0.0;
            if (!Helper::readReal(value, &number))
            {
                return PDFDocumentToolParameterHelper::tr("%1: the value must be a number.").arg(getLabel());
            }
            if (number < m_realMinimum || number > m_realMaximum)
            {
                return PDFDocumentToolParameterHelper::tr("%1: the value must be between %2 and %3.").arg(getLabel(), QString::number(m_realMinimum, 'g', 15), QString::number(m_realMaximum, 'g', 15));
            }
            result = number;
            return QString();
        }

        case PDFDocumentToolParameterType::Enum:
        {
            if (value.typeId() != QMetaType::QString)
            {
                return PDFDocumentToolParameterHelper::tr("%1: the value must be a text.").arg(getLabel());
            }

            const QString id = value.toString();
            for (const PDFDocumentToolParameterOption& option : m_options)
            {
                if (option.id == id)
                {
                    result = id;
                    return QString();
                }
            }
            return PDFDocumentToolParameterHelper::tr("%1: “%2” is not one of the allowed values.").arg(getLabel(), sanitizeForMessage(id));
        }

        case PDFDocumentToolParameterType::PageSelection:
        {
            QString errorMessage;
            const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(value, &errorMessage);
            if (!selection)
            {
                //: %1 is the name of the parameter, %2 the reason
                return PDFDocumentToolParameterHelper::tr("%1: %2").arg(getLabel(), errorMessage);
            }
            result = selection->toVariant();
            return QString();
        }

        case PDFDocumentToolParameterType::Color:
        {
            if (value.typeId() == QMetaType::QColor)
            {
                const QColor color = value.value<QColor>();
                if (color.isValid())
                {
                    result = QColor::fromRgb(color.rgb());
                    return QString();
                }
            }
            else if (value.typeId() == QMetaType::QString && Helper::isColorText(value.toString()))
            {
                result = QColor::fromString(value.toString());
                return QString();
            }
            return PDFDocumentToolParameterHelper::tr("%1: the color must be written as #rrggbb.").arg(getLabel());
        }

        default:
            // String, FilePath, FolderPath, FileNameTemplate, Secret
            return normalizeText(value, &result);
    }
}

QJsonObject PDFDocumentToolParameterDefinition::toJsonSchema() const
{
    using Helper = PDFDocumentToolParameterHelper;

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), Helper::getTypeName(m_type));
    if (!m_title.isEmpty())
    {
        schema.insert(QStringLiteral("title"), m_title);
    }

    QString description = m_description;
    switch (m_type)
    {
        case PDFDocumentToolParameterType::Integer:
            schema.insert(QStringLiteral("minimum"), m_integerMinimum);
            schema.insert(QStringLiteral("maximum"), m_integerMaximum);
            break;

        case PDFDocumentToolParameterType::Real:
            schema.insert(QStringLiteral("minimum"), m_realMinimum);
            schema.insert(QStringLiteral("maximum"), m_realMaximum);
            break;

        case PDFDocumentToolParameterType::String:
        case PDFDocumentToolParameterType::FilePath:
        case PDFDocumentToolParameterType::FolderPath:
        case PDFDocumentToolParameterType::FileNameTemplate:
            schema.insert(QStringLiteral("maxLength"), m_maximumLength);
            break;

        case PDFDocumentToolParameterType::Secret:
            schema.insert(QStringLiteral("maxLength"), m_maximumLength);
            schema.insert(QStringLiteral("writeOnly"), true);
            break;

        case PDFDocumentToolParameterType::Enum:
        {
            QJsonArray values;
            for (const PDFDocumentToolParameterOption& option : m_options)
            {
                values.append(option.id);
                QString line = QStringLiteral("\n- %1: %2").arg(option.id, option.title);
                if (!option.description.isEmpty())
                {
                    line += QStringLiteral(" (%1)").arg(option.description);
                }
                description += line;
            }
            schema.insert(QStringLiteral("enum"), values);
            break;
        }

        case PDFDocumentToolParameterType::Color:
            schema.insert(QStringLiteral("pattern"), QStringLiteral("^#[0-9a-fA-F]{6}$"));
            break;

        case PDFDocumentToolParameterType::PageSelection:
        {
            QJsonObject pages;
            pages.insert(QStringLiteral("type"), QStringLiteral("string"));
            pages.insert(QStringLiteral("maxLength"), PDFDocumentToolPageSelection::MAXIMUM_PAGES_LENGTH);
            pages.insert(QStringLiteral("description"), QStringLiteral("Pages numbered from 1, such as \"1-3, 5, 8-\"; empty = all pages"));

            QJsonObject filterItems;
            filterItems.insert(QStringLiteral("type"), QStringLiteral("string"));
            filterItems.insert(QStringLiteral("enum"), QJsonArray::fromStringList(PDFDocumentToolPageSelection::getFilterNames()));

            QJsonObject filters;
            filters.insert(QStringLiteral("type"), QStringLiteral("array"));
            filters.insert(QStringLiteral("items"), filterItems);
            filters.insert(QStringLiteral("uniqueItems"), true);
            filters.insert(QStringLiteral("description"), QStringLiteral("Even / odd pages and portrait / landscape pages; filters of one kind are joined, the two kinds narrow each other"));

            QJsonObject properties;
            properties.insert(QStringLiteral("pages"), pages);
            properties.insert(QStringLiteral("filters"), filters);
            schema.insert(QStringLiteral("properties"), properties);
            schema.insert(QStringLiteral("additionalProperties"), false);
            break;
        }

        case PDFDocumentToolParameterType::Bool:
            break;
    }

    if (!description.isEmpty())
    {
        schema.insert(QStringLiteral("description"), description);
    }

    if (!m_required && m_type != PDFDocumentToolParameterType::Secret)
    {
        schema.insert(QStringLiteral("default"), toJsonValue(m_defaultValue));
    }
    return schema;
}

QJsonValue PDFDocumentToolParameterDefinition::toJsonValue(const QVariant& value) const
{
    switch (m_type)
    {
        case PDFDocumentToolParameterType::Bool:
            return value.toBool();
        case PDFDocumentToolParameterType::Integer:
            return value.toLongLong();
        case PDFDocumentToolParameterType::Real:
            return value.toDouble();
        case PDFDocumentToolParameterType::Color:
            return value.value<QColor>().name(QColor::HexRgb);
        case PDFDocumentToolParameterType::PageSelection:
            return QJsonObject::fromVariantMap(value.toMap());
        case PDFDocumentToolParameterType::Secret:
            return QJsonValue();
        default:
            return value.toString();
    }
}

QVariant PDFDocumentToolParameterDefinition::toStorageValue(const QVariant& value) const
{
    switch (m_type)
    {
        case PDFDocumentToolParameterType::Bool:
            return qint64(value.toBool() ? 1 : 0);
        case PDFDocumentToolParameterType::Integer:
            return value.toLongLong();
        case PDFDocumentToolParameterType::Real:
            return value.toDouble();
        case PDFDocumentToolParameterType::Color:
            return value.value<QColor>().name(QColor::HexRgb);
        case PDFDocumentToolParameterType::PageSelection:
            return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(value.toMap())).toJson(QJsonDocument::Compact));
        case PDFDocumentToolParameterType::Secret:
            return QVariant();
        default:
            return value.toString();
    }
}

std::optional<QVariant> PDFDocumentToolParameterDefinition::fromStorageValue(const QVariant& storedValue) const
{
    QVariant candidate;
    switch (m_type)
    {
        case PDFDocumentToolParameterType::Secret:
            return std::nullopt;

        case PDFDocumentToolParameterType::Bool:
            if (storedValue.typeId() != QMetaType::LongLong || (storedValue.toLongLong() != 0 && storedValue.toLongLong() != 1))
            {
                return std::nullopt;
            }
            candidate = storedValue.toLongLong() != 0;
            break;

        case PDFDocumentToolParameterType::Integer:
            if (storedValue.typeId() != QMetaType::LongLong)
            {
                return std::nullopt;
            }
            candidate = storedValue;
            break;

        case PDFDocumentToolParameterType::Real:
            if (storedValue.typeId() != QMetaType::Double && storedValue.typeId() != QMetaType::LongLong)
            {
                return std::nullopt;
            }
            candidate = storedValue;
            break;

        case PDFDocumentToolParameterType::PageSelection:
        {
            if (storedValue.typeId() != QMetaType::QString || storedValue.toString().size() > PDFDocumentToolParameterHelper::MAXIMUM_STORED_JSON_LENGTH)
            {
                return std::nullopt;
            }

            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(storedValue.toString().toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
            {
                return std::nullopt;
            }
            candidate = document.object().toVariantMap();
            break;
        }

        default:
            if (storedValue.typeId() != QMetaType::QString)
            {
                return std::nullopt;
            }
            candidate = storedValue;
            break;
    }

    QVariant normalized;
    if (PDFDocumentToolParameterHelper::isMissing(candidate) || !normalize(candidate, &normalized).isEmpty())
    {
        return std::nullopt;
    }
    return normalized;
}

bool PDFDocumentToolParameters::isValid(const PDFDocumentToolParameterDefinitions& definitions, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (int(definitions.size()) > MAXIMUM_VALUE_COUNT)
    {
        error = QStringLiteral("Too many parameters.");
        return false;
    }

    QStringList names;
    for (const PDFDocumentToolParameterDefinition& definition : definitions)
    {
        if (!definition.isValid(&error))
        {
            return false;
        }
        if (names.contains(definition.getName()))
        {
            error = QStringLiteral("Duplicate parameter '%1'.").arg(definition.getName());
            return false;
        }
        names << definition.getName();
    }
    return true;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolParameters::normalize(const PDFDocumentToolParameterDefinitions& definitions, const QVariantMap& values, QVariantMap* normalizedValues)
{
    using Helper = PDFDocumentToolParameterHelper;

    std::vector<PDFDocumentToolParameterError> errors;
    QVariantMap result;

    if (values.size() > MAXIMUM_VALUE_COUNT)
    {
        errors.push_back({ QString(), PDFDocumentToolParameterHelper::tr("Too many parameters.") });
    }
    else
    {
        int unknownCount = 0;
        for (auto it = values.cbegin(); it != values.cend(); ++it)
        {
            if (!find(definitions, it.key()) && unknownCount++ < Helper::MAXIMUM_UNKNOWN_KEY_ERRORS)
            {
                const QString name = PDFDocumentToolParameterDefinition::sanitizeForMessage(it.key(), PDFDocumentToolParameterDefinition::MAXIMUM_NAME_LENGTH);
                errors.push_back({ name, PDFDocumentToolParameterHelper::tr("Unknown parameter “%1”.").arg(name) });
            }
        }

        for (const PDFDocumentToolParameterDefinition& definition : definitions)
        {
            QVariant normalized;
            const QString error = definition.normalize(values.value(definition.getName()), &normalized);
            if (!error.isEmpty())
            {
                errors.push_back({ definition.getName(), error });
            }
            else
            {
                result.insert(definition.getName(), normalized);
            }
        }
    }

    if (normalizedValues)
    {
        *normalizedValues = errors.empty() ? result : QVariantMap();
    }
    return errors;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolParameters::normalize(const PDFDocumentToolParameterDefinitions& definitions, const QJsonObject& values, QVariantMap* normalizedValues)
{
    if (values.size() > MAXIMUM_VALUE_COUNT)
    {
        if (normalizedValues)
        {
            normalizedValues->clear();
        }
        return { { QString(), PDFDocumentToolParameterHelper::tr("Too many parameters.") } };
    }
    return normalize(definitions, values.toVariantMap(), normalizedValues);
}

QVariantMap PDFDocumentToolParameters::getDefaultValues(const PDFDocumentToolParameterDefinitions& definitions)
{
    QVariantMap values;
    for (const PDFDocumentToolParameterDefinition& definition : definitions)
    {
        if (!definition.isRequired())
        {
            values.insert(definition.getName(), definition.getDefaultValue());
        }
    }
    return values;
}

QJsonObject PDFDocumentToolParameters::toJsonSchema(const PDFDocumentToolParameterDefinitions& definitions)
{
    QJsonObject properties;
    QJsonArray required;
    for (const PDFDocumentToolParameterDefinition& definition : definitions)
    {
        properties.insert(definition.getName(), definition.toJsonSchema());
        if (definition.isRequired())
        {
            required.append(definition.getName());
        }
    }

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    if (!required.isEmpty())
    {
        schema.insert(QStringLiteral("required"), required);
    }
    schema.insert(QStringLiteral("additionalProperties"), false);
    return schema;
}

QJsonObject PDFDocumentToolParameters::toJson(const PDFDocumentToolParameterDefinitions& definitions, const QVariantMap& values)
{
    QJsonObject object;
    for (const PDFDocumentToolParameterDefinition& definition : definitions)
    {
        if (definition.getType() != PDFDocumentToolParameterType::Secret && values.contains(definition.getName()))
        {
            object.insert(definition.getName(), definition.toJsonValue(values.value(definition.getName())));
        }
    }
    return object;
}

const PDFDocumentToolParameterDefinition* PDFDocumentToolParameters::find(const PDFDocumentToolParameterDefinitions& definitions, const QString& name)
{
    for (const PDFDocumentToolParameterDefinition& definition : definitions)
    {
        if (definition.getName() == name)
        {
            return &definition;
        }
    }
    return nullptr;
}

}   // namespace pdf
