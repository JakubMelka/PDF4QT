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

#include "pdfappannotationstylestorage.h"

#include "pdfadvancedtools.h"

#include <QJsonValue>

#include <array>
#include <cmath>
#include <utility>

namespace pdf
{

/// Tables of the stable names and the conversions of the values
class PDFAppAnnotationStyleStorageHelper
{
public:
    PDFAppAnnotationStyleStorageHelper() = delete;

    /// Maximal length of a stored name (font family, enumeration name)
    static constexpr qsizetype MAXIMUM_NAME_LENGTH = 256;

    static constexpr std::array<std::pair<TextAnnotationIcon, const char*>, 7> ICONS = { {
        { TextAnnotationIcon::Comment, "comment" },
        { TextAnnotationIcon::Help, "help" },
        { TextAnnotationIcon::Insert, "insert" },
        { TextAnnotationIcon::Key, "key" },
        { TextAnnotationIcon::NewParagraph, "newParagraph" },
        { TextAnnotationIcon::Note, "note" },
        { TextAnnotationIcon::Paragraph, "paragraph" } } };

    static constexpr std::array<std::pair<Stamp, const char*>, 14> STAMPS = { {
        { Stamp::Approved, "approved" },
        { Stamp::AsIs, "asIs" },
        { Stamp::Confidential, "confidential" },
        { Stamp::Departmental, "departmental" },
        { Stamp::Draft, "draft" },
        { Stamp::Experimental, "experimental" },
        { Stamp::Expired, "expired" },
        { Stamp::Final, "final" },
        { Stamp::ForComment, "forComment" },
        { Stamp::ForPublicRelease, "forPublicRelease" },
        { Stamp::NotApproved, "notApproved" },
        { Stamp::NotForPublicRelease, "notForPublicRelease" },
        { Stamp::Sold, "sold" },
        { Stamp::TopSecret, "topSecret" } } };

    static constexpr std::array<std::pair<DestinationType, const char*>, 8> DESTINATIONS = { {
        { DestinationType::XYZ, "xyz" },
        { DestinationType::Fit, "fit" },
        { DestinationType::FitH, "fitH" },
        { DestinationType::FitV, "fitV" },
        { DestinationType::FitR, "fitR" },
        { DestinationType::FitB, "fitB" },
        { DestinationType::FitBH, "fitBH" },
        { DestinationType::FitBV, "fitBV" } } };

    static constexpr std::array<std::pair<AnnotationType, const char*>, 4> MARKUPS = { {
        { AnnotationType::Highlight, "highlight" },
        { AnnotationType::Underline, "underline" },
        { AnnotationType::Squiggly, "squiggly" },
        { AnnotationType::StrikeOut, "strikeOut" } } };

    template<typename T, size_t N>
    static QString getName(const std::array<std::pair<T, const char*>, N>& table, T value)
    {
        for (const auto& item : table)
        {
            if (item.first == value)
            {
                return QString::fromLatin1(item.second);
            }
        }
        return QString();
    }

    template<typename T, size_t N>
    static std::optional<T> getValue(const std::array<std::pair<T, const char*>, N>& table, const QString& name)
    {
        if (name.isEmpty() || name.size() > MAXIMUM_NAME_LENGTH)
        {
            return std::nullopt;
        }
        for (const auto& item : table)
        {
            if (name == QLatin1String(item.second))
            {
                return item.first;
            }
        }
        return std::nullopt;
    }

    /// Color of a JSON value ("#RRGGBB" / "#AARRGGBB"), nullopt for anything else
    static std::optional<QColor> getColor(const QJsonValue& value)
    {
        if (!value.isString())
        {
            return std::nullopt;
        }
        const QString text = value.toString();
        if (text.size() > 16 || !text.startsWith(QLatin1Char('#')))
        {
            return std::nullopt;
        }
        const QColor color = QColor::fromString(text);
        if (!color.isValid())
        {
            return std::nullopt;
        }
        return color;
    }

    static QString getColorText(const QColor& color)
    {
        return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
    }

    /// Finite number of a JSON value, nullopt otherwise
    static std::optional<double> getNumber(const QJsonValue& value)
    {
        if (!value.isDouble())
        {
            return std::nullopt;
        }
        const double number = value.toDouble();
        if (!std::isfinite(number))
        {
            return std::nullopt;
        }
        return number;
    }
};

PDFAppAnnotationStyleStorage::PDFAppAnnotationStyleStorage(PDFDatabase* database) :
    m_database(database)
{

}

PDFAppAnnotationStyleStorage::~PDFAppAnnotationStyleStorage() = default;

PDFAnnotationStyle PDFAppAnnotationStyleStorage::getStyle(const QString& styleId, const PDFAnnotationStyle& defaultStyle) const
{
    const std::optional<QJsonObject> value = getRepository().getValue(styleId);
    if (!value)
    {
        return PDFAnnotationStyleSettings::boundStyle(defaultStyle, defaultStyle);
    }
    return fromJson(*value, defaultStyle);
}

void PDFAppAnnotationStyleStorage::setStyle(const QString& styleId, const PDFAnnotationStyle& style)
{
    setValue(styleId, toJson(style));
}

TextAnnotationIcon PDFAppAnnotationStyleStorage::getStickyNoteIcon(TextAnnotationIcon defaultIcon) const
{
    return getIconFromName(getString(PDFAnnotationStyleRepository::toolStickyNote(), QStringLiteral("icon"))).value_or(defaultIcon);
}

void PDFAppAnnotationStyleStorage::setStickyNoteIcon(TextAnnotationIcon icon)
{
    const QString name = getIconName(icon);
    if (!name.isEmpty())
    {
        setValue(PDFAnnotationStyleRepository::toolStickyNote(), QJsonObject({ { QStringLiteral("icon"), name } }));
    }
}

Stamp PDFAppAnnotationStyleStorage::getStamp(Stamp defaultStamp) const
{
    return getStampFromName(getString(PDFAnnotationStyleRepository::toolStamp(), QStringLiteral("stamp"))).value_or(defaultStamp);
}

void PDFAppAnnotationStyleStorage::setStamp(Stamp stamp)
{
    const QString name = getStampName(stamp);
    if (!name.isEmpty())
    {
        setValue(PDFAnnotationStyleRepository::toolStamp(), QJsonObject({ { QStringLiteral("stamp"), name } }));
    }
}

PDFFreeTextStyle PDFAppAnnotationStyleStorage::getFreeTextStyle(const PDFFreeTextStyle& defaultStyle, bool* autoResize) const
{
    using H = PDFAppAnnotationStyleStorageHelper;

    PDFFreeTextStyle style = defaultStyle;
    bool isAutoResize = true;

    if (const std::optional<QJsonObject> value = getRepository().getValue(PDFAnnotationStyleRepository::toolFreeText()))
    {
        const QJsonValue font = value->value(QStringLiteral("font"));
        if (font.isString() && !font.toString().isEmpty() && font.toString().size() <= H::MAXIMUM_NAME_LENGTH)
        {
            style.fontFamily = font.toString();
        }
        if (const std::optional<double> size = H::getNumber(value->value(QStringLiteral("size"))))
        {
            style.fontSize = *size;
        }
        if (const std::optional<QColor> color = H::getColor(value->value(QStringLiteral("textColor"))))
        {
            style.textColor = *color;
        }
        if (const std::optional<double> alignment = H::getNumber(value->value(QStringLiteral("alignment"))))
        {
            const int mask = int(Qt::AlignHorizontal_Mask | Qt::AlignVertical_Mask);
            if (*alignment >= 0.0 && *alignment <= double(mask))
            {
                style.textAlignment = TextAlignment(int(*alignment) & mask);
            }
        }
        const QJsonValue resize = value->value(QStringLiteral("autoResize"));
        if (resize.isBool())
        {
            isAutoResize = resize.toBool();
        }
    }

    if (autoResize)
    {
        *autoResize = isAutoResize;
    }
    return PDFCreateFreeTextTool::boundFreeTextStyle(style);
}

void PDFAppAnnotationStyleStorage::setFreeTextStyle(const PDFFreeTextStyle& style, bool autoResize)
{
    const PDFFreeTextStyle bounded = PDFCreateFreeTextTool::boundFreeTextStyle(style);
    QJsonObject value;
    value.insert(QStringLiteral("font"), bounded.fontFamily.left(PDFAppAnnotationStyleStorageHelper::MAXIMUM_NAME_LENGTH));
    value.insert(QStringLiteral("size"), bounded.fontSize);
    value.insert(QStringLiteral("textColor"), PDFAppAnnotationStyleStorageHelper::getColorText(bounded.textColor));
    value.insert(QStringLiteral("alignment"), int(bounded.textAlignment.toInt()));
    value.insert(QStringLiteral("autoResize"), autoResize);
    setValue(PDFAnnotationStyleRepository::toolFreeText(), value);
}

DestinationType PDFAppAnnotationStyleStorage::getPageLinkDestinationType(DestinationType defaultType) const
{
    return getDestinationTypeFromName(getString(PDFAnnotationStyleRepository::toolPageLink(), QStringLiteral("destinationType"))).value_or(defaultType);
}

void PDFAppAnnotationStyleStorage::setPageLinkDestinationType(DestinationType type)
{
    const QString name = getDestinationTypeName(type);
    if (!name.isEmpty())
    {
        setValue(PDFAnnotationStyleRepository::toolPageLink(), QJsonObject({ { QStringLiteral("destinationType"), name } }));
    }
}

AnnotationType PDFAppAnnotationStyleStorage::getLastMarkupType(AnnotationType defaultType) const
{
    return getMarkupTypeFromName(getString(PDFAnnotationStyleRepository::toolLastMarkup(), QStringLiteral("type"))).value_or(defaultType);
}

void PDFAppAnnotationStyleStorage::setLastMarkupType(AnnotationType type)
{
    const QString name = getMarkupTypeName(type);
    if (!name.isEmpty())
    {
        setValue(PDFAnnotationStyleRepository::toolLastMarkup(), QJsonObject({ { QStringLiteral("type"), name } }));
    }
}

QJsonObject PDFAppAnnotationStyleStorage::toJson(const PDFAnnotationStyle& style)
{
    QJsonObject value;
    value.insert(QStringLiteral("strokeColor"), style.strokeColor.isValid() ? QJsonValue(PDFAppAnnotationStyleStorageHelper::getColorText(style.strokeColor)) : QJsonValue());
    value.insert(QStringLiteral("fillColor"), style.fillColor.isValid() ? QJsonValue(PDFAppAnnotationStyleStorageHelper::getColorText(style.fillColor)) : QJsonValue());
    value.insert(QStringLiteral("penWidth"), std::isfinite(style.penWidth) ? QJsonValue(style.penWidth) : QJsonValue());
    return value;
}

PDFAnnotationStyle PDFAppAnnotationStyleStorage::fromJson(const QJsonObject& value, const PDFAnnotationStyle& defaultStyle)
{
    using H = PDFAppAnnotationStyleStorageHelper;

    PDFAnnotationStyle style = defaultStyle;
    if (const std::optional<QColor> color = H::getColor(value.value(QStringLiteral("strokeColor"))))
    {
        style.strokeColor = *color;
    }

    // null = no fill, a color = the fill, anything else = the default fill
    const QJsonValue fill = value.value(QStringLiteral("fillColor"));
    if (fill.isNull())
    {
        style.fillColor = QColor();
    }
    else if (const std::optional<QColor> color = H::getColor(fill))
    {
        style.fillColor = *color;
    }

    if (const std::optional<double> penWidth = H::getNumber(value.value(QStringLiteral("penWidth"))))
    {
        style.penWidth = *penWidth;
    }
    return PDFAnnotationStyleSettings::boundStyle(style, defaultStyle);
}

QString PDFAppAnnotationStyleStorage::getIconName(TextAnnotationIcon icon)
{
    return PDFAppAnnotationStyleStorageHelper::getName(PDFAppAnnotationStyleStorageHelper::ICONS, icon);
}

std::optional<TextAnnotationIcon> PDFAppAnnotationStyleStorage::getIconFromName(const QString& name)
{
    return PDFAppAnnotationStyleStorageHelper::getValue(PDFAppAnnotationStyleStorageHelper::ICONS, name);
}

QString PDFAppAnnotationStyleStorage::getStampName(Stamp stamp)
{
    return PDFAppAnnotationStyleStorageHelper::getName(PDFAppAnnotationStyleStorageHelper::STAMPS, stamp);
}

std::optional<Stamp> PDFAppAnnotationStyleStorage::getStampFromName(const QString& name)
{
    return PDFAppAnnotationStyleStorageHelper::getValue(PDFAppAnnotationStyleStorageHelper::STAMPS, name);
}

QString PDFAppAnnotationStyleStorage::getDestinationTypeName(DestinationType type)
{
    return PDFAppAnnotationStyleStorageHelper::getName(PDFAppAnnotationStyleStorageHelper::DESTINATIONS, type);
}

std::optional<DestinationType> PDFAppAnnotationStyleStorage::getDestinationTypeFromName(const QString& name)
{
    return PDFAppAnnotationStyleStorageHelper::getValue(PDFAppAnnotationStyleStorageHelper::DESTINATIONS, name);
}

QString PDFAppAnnotationStyleStorage::getMarkupTypeName(AnnotationType type)
{
    return PDFAppAnnotationStyleStorageHelper::getName(PDFAppAnnotationStyleStorageHelper::MARKUPS, type);
}

std::optional<AnnotationType> PDFAppAnnotationStyleStorage::getMarkupTypeFromName(const QString& name)
{
    return PDFAppAnnotationStyleStorageHelper::getValue(PDFAppAnnotationStyleStorageHelper::MARKUPS, name);
}

QString PDFAppAnnotationStyleStorage::getString(const QString& styleId, const QString& key) const
{
    const std::optional<QJsonObject> value = getRepository().getValue(styleId);
    const QJsonValue item = value ? value->value(key) : QJsonValue();
    return item.isString() ? item.toString() : QString();
}

void PDFAppAnnotationStyleStorage::setValue(const QString& styleId, const QJsonObject& value)
{
    m_lastError = getRepository().setValue(styleId, value);
    if (m_lastError.isError())
    {
        qWarning("Annotation style %s cannot be stored: %s", qPrintable(styleId), qPrintable(m_lastError.toString()));
    }
}

}   // namespace pdf
