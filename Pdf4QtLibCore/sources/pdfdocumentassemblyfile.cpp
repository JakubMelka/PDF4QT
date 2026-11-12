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

#include "pdfdocumentassemblyfile.h"

#include "pdfimagepagefactory.h"
#include "pdfpagesizepresets.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>

namespace pdf
{

/// Helpers of the assembly file (plan of step 3b of phase 2, 4.3, 6, RB7, RB8; package M1).
///
/// The pages of an item are the text of their runs (RB7: the own reader of this class with the
/// limits): tokens separated by commas; "<letters><a>[-<b>][@<turn>][+][[<l> <t> <r> <b>]]" the
/// pages a to b of the source of the letters (A = the first source of "sources"; from 1, a
/// descending range is reversed), turned clockwise by 90, 180 or 270, "+" a clone (RB4), the crop
/// margins in points; "*<width>x<height>[@<turn>]" a blank page of the size in points. A run joins
/// only pages of one source with the same turn, clone flag and crop.
class PDFDocumentAssemblyFileHelper
{
public:
    PDFDocumentAssemblyFileHelper() = delete;

    using Assembly = PDFDocumentAssembly;
    using Page = PDFDocumentAssembly::Page;
    using Item = PDFDocumentAssembly::Item;
    using Source = PDFDocumentAssembly::Source;
    using File = PDFDocumentAssemblyFile;

    /// The keys of the output parameters, which the file keeps (Content::outputParameters)
    static QStringList getOutputKeys()
    {
        return { QStringLiteral("output"), QStringLiteral("outline"), QStringLiteral("optimizeImages"),
                 QStringLiteral("folder"), QStringLiteral("fileName"), QStringLiteral("existingFiles") };
    }

    static QString formatNumber(double value)
    {
        return QString::number(value, 'g', 12);
    }

    static QString getTurnSuffix(PageRotation turn)
    {
        switch (turn)
        {
            case PageRotation::Rotate90:
                return QStringLiteral("@90");
            case PageRotation::Rotate180:
                return QStringLiteral("@180");
            case PageRotation::Rotate270:
                return QStringLiteral("@270");
            default:
                return QString();
        }
    }

    /// The text of the pages of an item (see the class)
    static QString writePages(const std::vector<Page>& pages)
    {
        QStringList tokens;
        size_t i = 0;
        while (i < pages.size())
        {
            const PDFDocumentPagePlan::Item& first = pages[i].plan;
            if (first.input < 0)
            {
                tokens << QStringLiteral("*%1x%2%3").arg(formatNumber(first.pageSize.width()), formatNumber(first.pageSize.height()), getTurnSuffix(first.turn));
                ++i;
                continue;
            }

            // A run of the next pages of one source by the step +1 or -1
            size_t last = i;
            PDFInteger step = 0;
            while (last + 1 < pages.size())
            {
                const PDFDocumentPagePlan::Item& next = pages[last + 1].plan;
                const PDFDocumentPagePlan::Item& current = pages[last].plan;
                const PDFInteger difference = next.pageIndex - current.pageIndex;
                const bool isSameKind = next.input == first.input && next.turn == first.turn && next.isClone == first.isClone && next.cropMargins == first.cropMargins;
                if (!isSameKind || (difference != 1 && difference != -1) || (step != 0 && difference != step))
                {
                    break;
                }
                step = difference;
                ++last;
            }

            QString token = PDFDocumentPagePlan::getInputLetters(first.input) + QString::number(first.pageIndex + 1);
            if (last != i)
            {
                token += QStringLiteral("-%1").arg(pages[last].plan.pageIndex + 1);
            }
            token += getTurnSuffix(first.turn);
            if (first.isClone)
            {
                token += QLatin1Char('+');
            }
            if (!first.cropMargins.isNull())
            {
                token += QStringLiteral("[%1 %2 %3 %4]").arg(formatNumber(first.cropMargins.left()), formatNumber(first.cropMargins.top()),
                                                             formatNumber(first.cropMargins.right()), formatNumber(first.cropMargins.bottom()));
            }
            tokens << token;
            i = last + 1;
        }
        return tokens.join(QLatin1Char(','));
    }

    /// A number of a token: digits only, at most 10 of them
    static std::optional<PDFInteger> readInteger(QStringView text)
    {
        if (text.isEmpty() || text.size() > 10)
        {
            return std::nullopt;
        }
        for (const QChar character : text)
        {
            if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            {
                return std::nullopt;
            }
        }
        return text.toLongLong();
    }

    /// A finite number of a token (C locale)
    static std::optional<double> readReal(QStringView text)
    {
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (!ok || !std::isfinite(value))
        {
            return std::nullopt;
        }
        return value;
    }

    /// The turn suffix at the end of \p text ("@90"), removed from it; false for an invalid one
    static bool readTurn(QStringView* text, PageRotation* turn)
    {
        *turn = PageRotation::None;
        const qsizetype at = text->indexOf(QLatin1Char('@'));
        if (at < 0)
        {
            return true;
        }
        const QStringView value = text->mid(at + 1);
        *text = text->left(at);
        if (value == QLatin1String("90"))
        {
            *turn = PageRotation::Rotate90;
        }
        else if (value == QLatin1String("180"))
        {
            *turn = PageRotation::Rotate180;
        }
        else if (value == QLatin1String("270"))
        {
            *turn = PageRotation::Rotate270;
        }
        else if (value != QLatin1String("0"))
        {
            return false;
        }
        return true;
    }

    /// Reads the text of the pages of an item; \p pageTotal counts the pages of the whole file
    /// (the limit before a range is expanded); false and the reason. Without \p pages the text is
    /// only counted (countPages): nothing is expanded.
    static bool readPages(const QString& text, size_t sourceCount, qint64 maximumPageTotal, qint64* pageTotal, std::vector<Page>* pages, QString* errorMessage)
    {
        for (const QStringView part : QStringView(text).split(QLatin1Char(',')))
        {
            QStringView token = part.trimmed();
            if (token.isEmpty())
            {
                *errorMessage = File::tr("An item has an empty page.");
                return false;
            }

            Page page;
            if (token.front() == QLatin1Char('*'))
            {
                token = token.mid(1);
                PageRotation turn = PageRotation::None;
                const qsizetype separator = token.indexOf(QLatin1Char('x'));
                if (!readTurn(&token, &turn) || separator < 0)
                {
                    *errorMessage = File::tr("A blank page has no valid size.");
                    return false;
                }
                const std::optional<double> width = readReal(token.left(separator));
                const std::optional<double> height = readReal(token.mid(separator + 1));
                if (!width || !height || !PDFPageSizePresets::isValidSide(*width) || !PDFPageSizePresets::isValidSide(*height))
                {
                    *errorMessage = File::tr("A blank page has no valid size.");
                    return false;
                }
                if (++*pageTotal > maximumPageTotal)
                {
                    *errorMessage = File::tr("The assembly has too many pages.");
                    return false;
                }
                page.plan.input = Assembly::BLANK_INPUT;
                page.plan.pageSize = QSizeF(*width, *height);
                page.plan.turn = turn;
                if (pages)
                {
                    pages->push_back(std::move(page));
                }
                continue;
            }

            // The crop
            QMarginsF crop;
            if (token.endsWith(QLatin1Char(']')))
            {
                const qsizetype open = token.indexOf(QLatin1Char('['));
                if (open < 0)
                {
                    *errorMessage = File::tr("A page has an invalid crop.");
                    return false;
                }
                const QList<QStringView> values = token.mid(open + 1, token.size() - open - 2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                std::vector<double> margins;
                for (const QStringView value : values)
                {
                    const std::optional<double> margin = readReal(value);
                    if (!margin || *margin < 0.0)
                    {
                        break;
                    }
                    margins.push_back(*margin);
                }
                if (values.size() != 4 || margins.size() != 4)
                {
                    *errorMessage = File::tr("A page has an invalid crop.");
                    return false;
                }
                crop = QMarginsF(margins[0], margins[1], margins[2], margins[3]);
                token = token.left(open);
            }

            const bool isClone = token.endsWith(QLatin1Char('+'));
            if (isClone)
            {
                token.chop(1);
            }
            PageRotation turn = PageRotation::None;
            if (!readTurn(&token, &turn))
            {
                *errorMessage = File::tr("A page has an invalid rotation.");
                return false;
            }

            qsizetype letters = 0;
            while (letters < token.size() && token[letters] >= QLatin1Char('A') && token[letters] <= QLatin1Char('Z'))
            {
                ++letters;
            }
            const int input = letters > 0 && letters <= PDFDocumentPagePlan::MAXIMUM_LETTERS ? PDFDocumentPagePlan::getInputIndex(token.left(letters)) : -1;
            if (input < 0 || size_t(input) >= sourceCount)
            {
                *errorMessage = File::tr("A page belongs to no file of the assembly.");
                return false;
            }

            const QStringView range = token.mid(letters);
            const qsizetype dash = range.indexOf(QLatin1Char('-'));
            const std::optional<PDFInteger> firstPage = readInteger(dash < 0 ? range : range.left(dash));
            const std::optional<PDFInteger> lastPage = dash < 0 ? firstPage : readInteger(range.mid(dash + 1));
            if (!firstPage || !lastPage || *firstPage < 1 || *lastPage < 1)
            {
                *errorMessage = File::tr("A page of the assembly has no valid number.");
                return false;
            }

            const qint64 count = std::abs(*lastPage - *firstPage) + 1;
            if (*pageTotal + count > maximumPageTotal)
            {
                *errorMessage = File::tr("The assembly has too many pages.");
                return false;
            }
            *pageTotal += count;
            if (!pages)
            {
                continue;
            }

            const PDFInteger step = *lastPage >= *firstPage ? 1 : -1;
            for (PDFInteger number = *firstPage;; number += step)
            {
                Page rangePage;
                rangePage.plan.input = input;
                rangePage.plan.pageIndex = number - 1;
                rangePage.plan.turn = turn;
                rangePage.plan.isClone = isClone;
                rangePage.plan.cropMargins = crop;
                pages->push_back(std::move(rangePage));
                if (number == *lastPage)
                {
                    break;
                }
            }
        }
        return true;
    }

    /// The depth of a JSON value is at most \p maximumDepth (checked after the parsing, RB8)
    static bool checkDepth(const QJsonValue& value, int depth, int maximumDepth)
    {
        if (depth > maximumDepth)
        {
            return false;
        }
        if (value.isArray())
        {
            const QJsonArray array = value.toArray();
            for (const QJsonValue& item : array)
            {
                if (!checkDepth(item, depth + 1, maximumDepth))
                {
                    return false;
                }
            }
        }
        else if (value.isObject())
        {
            const QJsonObject object = value.toObject();
            for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            {
                if (!checkDepth(it.value(), depth + 1, maximumDepth))
                {
                    return false;
                }
            }
        }
        return true;
    }

    /// A text value of an object: missing = empty, another type = false
    static bool readString(const QJsonObject& object, const char* key, int maximumLength, QString* value)
    {
        const QJsonValue jsonValue = object.value(QLatin1String(key));
        if (jsonValue.isUndefined() || jsonValue.isNull())
        {
            value->clear();
            return true;
        }
        if (!jsonValue.isString() || jsonValue.toString().size() > maximumLength)
        {
            return false;
        }
        *value = jsonValue.toString();
        return true;
    }

    /// An integer value of an object: missing = \p defaultValue, another type or a fraction = false
    static bool readInteger(const QJsonObject& object, const char* key, qint64 minimum, qint64 maximum, qint64 defaultValue, qint64* value)
    {
        const QJsonValue jsonValue = object.value(QLatin1String(key));
        if (jsonValue.isUndefined() || jsonValue.isNull())
        {
            *value = defaultValue;
            return true;
        }
        const double number = jsonValue.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!jsonValue.isDouble() || !std::isfinite(number) || std::floor(number) != number || number < double(minimum) || number > double(maximum))
        {
            return false;
        }
        *value = qint64(number);
        return true;
    }

    /// A boolean value of an object: missing = false, another type = false (refused)
    static bool readBool(const QJsonObject& object, const char* key, bool* value)
    {
        const QJsonValue jsonValue = object.value(QLatin1String(key));
        if (jsonValue.isUndefined() || jsonValue.isNull())
        {
            *value = false;
            return true;
        }
        if (!jsonValue.isBool())
        {
            return false;
        }
        *value = jsonValue.toBool();
        return true;
    }

    static QJsonArray writeItems(const std::vector<Item>& items)
    {
        QJsonArray array;
        for (const Item& item : items)
        {
            QJsonObject object;
            object.insert(QStringLiteral("id"), item.id);
            if (!item.name.isEmpty())
            {
                object.insert(QStringLiteral("name"), item.name);
            }
            if (item.isCreatedGroup)
            {
                object.insert(QStringLiteral("group"), true);
            }
            object.insert(QStringLiteral("pages"), writePages(item.pages));
            array.append(object);
        }
        return array;
    }

    /// Reads the items of an array; the pages outside of their sources go to \p outOfRange (the
    /// index of their item in \p items, or -1 when the whole item is out of range)
    static bool readItems(const QJsonValue& value, const std::vector<Source>& sources, const File::Limits& limits, qint64 maximumPageTotal, qint64* pageTotal,
                          std::vector<Item>* items, std::vector<std::pair<int, Item>>* outOfRange, QString* errorMessage)
    {
        if (!value.isArray())
        {
            *errorMessage = File::tr("The items of the assembly are missing.");
            return false;
        }
        const QJsonArray array = value.toArray();
        if (array.size() > limits.maximumItemCount)
        {
            *errorMessage = File::tr("The assembly has more than %Ln items.", nullptr, limits.maximumItemCount);
            return false;
        }

        QSet<QString> ids;
        for (const QJsonValue& itemValue : array)
        {
            const QJsonObject object = itemValue.toObject();
            Item item;
            QString pagesText;
            if (!itemValue.isObject() || !readString(object, "id", limits.maximumNameLength, &item.id) || item.id.isEmpty() || ids.contains(item.id) ||
                !readString(object, "name", limits.maximumNameLength, &item.name) || !readBool(object, "group", &item.isCreatedGroup) ||
                !object.value(QLatin1String("pages")).isString())
            {
                *errorMessage = File::tr("An item of the assembly is not valid.");
                return false;
            }
            ids.insert(item.id);
            pagesText = object.value(QLatin1String("pages")).toString();

            std::vector<Page> pages;
            if (!readPages(pagesText, sources.size(), maximumPageTotal, pageTotal, &pages, errorMessage))
            {
                return false;
            }

            // A page over the page count of its source (a changed source, a hostile file) goes to the bin
            Item removed;
            for (Page& page : pages)
            {
                const bool isInRange = page.plan.input < 0 || page.plan.pageIndex < sources[size_t(page.plan.input)].pageCount;
                (isInRange ? item.pages : removed.pages).push_back(std::move(page));
            }
            if (!removed.pages.empty())
            {
                if (item.pages.empty())
                {
                    removed.id = item.id;
                    removed.name = item.name;
                    removed.isCreatedGroup = item.isCreatedGroup;
                    outOfRange->emplace_back(-1, std::move(removed));
                    continue;
                }
                outOfRange->emplace_back(int(items->size()), std::move(removed));
            }
            if (item.pages.empty())
            {
                *errorMessage = File::tr("An item of the assembly has no page.");
                return false;
            }
            items->push_back(std::move(item));
        }
        return true;
    }

    /// Adds the pages of the items of an array to \p pageTotal without expanding a range (owner Q3
    /// of 2026-11-12: the limit before anything is allocated). Every text, which read could expand,
    /// is counted - also of an item, which read refuses later; a text with an invalid token is
    /// counted up to it (read stops there with its error). Not an array = nothing (read refuses it).
    static void countPages(const QJsonValue& value, size_t sourceCount, qint64* pageTotal)
    {
        // No overflow: the total never exceeds this bound, a token adds at most 10^10 pages
        const qint64 unlimited = std::numeric_limits<qint64>::max() / 4;
        const QJsonArray array = value.toArray();
        for (const QJsonValue& itemValue : array)
        {
            const QJsonValue pagesValue = itemValue.toObject().value(QLatin1String("pages"));
            if (pagesValue.isString())
            {
                QString ignored;
                readPages(pagesValue.toString(), sourceCount, unlimited, pageTotal, nullptr, &ignored);
            }
        }
    }

    /// A count of a text (%Ln takes an int)
    static int toCount(qint64 value)
    {
        return int(std::clamp<qint64>(value, 0, std::numeric_limits<int>::max()));
    }

    /// The refusals of the pages of a file: the items, a checkpoint, all of them together
    static QString getItemPagesText(qint64 limit)
    {
        return File::tr("The assembly has more than %Ln pages.", nullptr, toCount(limit));
    }

    static QString getCheckpointPagesText(qint64 limit)
    {
        return File::tr("A checkpoint of the assembly has more than %Ln pages.", nullptr, toCount(limit));
    }

    static QString getFilePagesText(qint64 limit)
    {
        return File::tr("The assembly and its checkpoints have more than %Ln pages.", nullptr, toCount(limit));
    }

    /// The pages of the items
    static qint64 getPageCount(const std::vector<Item>& items)
    {
        qint64 count = 0;
        for (const Item& item : items)
        {
            count += qint64(item.pages.size());
        }
        return count;
    }

    /// Reads the sources (see the contract of the class); \p problems gets the images, which
    /// cannot be read
    static bool readSources(const QJsonValue& value, const File::Limits& limits, std::vector<Source>* sources, std::vector<File::Problem>* problems, QString* errorMessage)
    {
        if (!value.isArray())
        {
            *errorMessage = File::tr("The files of the assembly are missing.");
            return false;
        }
        const QJsonArray array = value.toArray();
        if (array.size() > limits.maximumSourceCount)
        {
            *errorMessage = File::tr("The assembly has more than %Ln files.", nullptr, limits.maximumSourceCount);
            return false;
        }

        QSet<QString> ids;
        for (const QJsonValue& sourceValue : array)
        {
            const QJsonObject object = sourceValue.toObject();
            Source source;
            QString kind;
            QString modified;
            QString sha256;
            QString format;
            QString data;
            qint64 size = -1;
            qint64 pageCount = 0;
            const bool isRead = sourceValue.isObject() &&
                                readString(object, "id", limits.maximumNameLength, &source.id) && !source.id.isEmpty() && !ids.contains(source.id) &&
                                readString(object, "kind", 16, &kind) &&
                                readString(object, "name", limits.maximumNameLength, &source.name) &&
                                readString(object, "path", 32767, &source.path) &&
                                readString(object, "relativePath", 32767, &source.relativePath) &&
                                readInteger(object, "size", -1, std::numeric_limits<qint64>::max() / 2, -1, &size) &&
                                readString(object, "modified", 64, &modified) &&
                                readString(object, "sha256", 64, &sha256) &&
                                readInteger(object, "pageCount", 0, limits.maximumPageCount, 0, &pageCount) &&
                                readBool(object, "parked", &source.isParked) &&
                                readString(object, "format", 32, &format) &&
                                (object.value(QLatin1String("data")).isUndefined() || object.value(QLatin1String("data")).isString());
            if (!isRead)
            {
                *errorMessage = File::tr("A file of the assembly is not valid.");
                return false;
            }
            ids.insert(source.id);
            source.size = size;
            source.pageCount = pageCount;
            if (!modified.isEmpty())
            {
                source.modified = QDateTime::fromString(modified, Qt::ISODateWithMs);
                if (!source.modified.isValid())
                {
                    *errorMessage = File::tr("A file of the assembly has an invalid time.");
                    return false;
                }
            }
            if (!sha256.isEmpty())
            {
                source.sha256 = QByteArray::fromHex(sha256.toLatin1());
                if (source.sha256.size() != 32 || source.sha256.toHex() != sha256.toLower().toLatin1())
                {
                    *errorMessage = File::tr("A file of the assembly has an invalid hash.");
                    return false;
                }
            }

            if (kind == QLatin1String("pdf"))
            {
                source.kind = Assembly::SourceKind::Pdf;
                if (source.pageCount < 1)
                {
                    *errorMessage = File::tr("A file of the assembly has no pages.");
                    return false;
                }
            }
            else if (kind == QLatin1String("image"))
            {
                // Q11: the image is in the file; its bytes are read by the limits of the images (R15)
                source.kind = Assembly::SourceKind::Image;
                source.imageFormat = format.toLatin1();
                const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(object.value(QLatin1String("data")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
                if (!decoded || decoded.decoded.isEmpty())
                {
                    *errorMessage = File::tr("An image of the assembly has no valid data.");
                    return false;
                }
                source.imageData = decoded.decoded;
                const PDFImagePageFactory::ImageInfo info = PDFImagePageFactory::readInfo(source.imageData, source.imageFormat);
                if (info.frameCount != source.pageCount)
                {
                    problems->push_back({ source.id, File::tr("The image %1 cannot be read as it was saved: %2").arg(source.name, info.frameCount > 0 ? File::tr("it has %Ln pages", nullptr, info.frameCount) : info.error) });
                    source.pageCount = info.frameCount;
                }
            }
            else
            {
                *errorMessage = File::tr("A file of the assembly has an unknown kind.");
                return false;
            }
            sources->push_back(std::move(source));
        }
        return true;
    }
};

PDFDocumentAssemblyFile::ReadResult PDFDocumentAssemblyFile::read(const QByteArray& data, const Limits& limits)
{
    using Helper = PDFDocumentAssemblyFileHelper;

    ReadResult result;
    if (data.size() > limits.maximumFileSize)
    {
        result.errorMessage = tr("The assembly file is larger than %1 MB.").arg(limits.maximumFileSize / (1024 * 1024));
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        result.errorMessage = tr("The assembly file is damaged: %1").arg(parseError.error != QJsonParseError::NoError ? parseError.errorString() : tr("it is no JSON object"));
        return result;
    }
    if (!Helper::checkDepth(document.object(), 1, limits.maximumDepth))
    {
        result.errorMessage = tr("The assembly file is damaged: it is nested too deep.");
        return result;
    }

    const QJsonObject root = document.object();
    if (root.value(QLatin1String("format")).toString() != QLatin1String(FORMAT_NAME))
    {
        result.errorMessage = tr("This is not an assembly file.");
        return result;
    }
    qint64 version = 0;
    if (!Helper::readInteger(root, "version", 1, std::numeric_limits<int>::max(), 0, &version) || version < 1)
    {
        result.errorMessage = tr("This is not an assembly file.");
        return result;
    }
    if (version > VERSION)
    {
        result.errorMessage = tr("This assembly was saved by a newer version of PDF4QT.");
        return result;
    }

    std::vector<PDFDocumentAssembly::Source> sources;
    if (!Helper::readSources(root.value(QLatin1String("sources")), limits, &sources, &result.problems, &result.errorMessage))
    {
        return result;
    }

    // Owner 2026-11-12 (plan of step 3b, 13 point 3): the pages of the items and of all
    // checkpoints are bounded together, counted before any range is expanded (a range of ten
    // characters is a million pages, a file of a kilobyte would allocate gigabytes); the items and
    // each checkpoint alone by the pages of an assembly
    const QJsonValue itemsValue = root.value(QLatin1String("items"));
    const QJsonValue checkpointsValue = root.value(QLatin1String("checkpoints"));
    qint64 countedItemPages = 0;
    Helper::countPages(itemsValue, sources.size(), &countedItemPages);
    if (countedItemPages > limits.maximumPageCount)
    {
        result.errorMessage = Helper::getItemPagesText(limits.maximumPageCount);
        return result;
    }
    qint64 countedFilePages = countedItemPages;
    for (const QJsonValue& checkpointValue : checkpointsValue.toArray())
    {
        qint64 countedCheckpointPages = 0;
        Helper::countPages(checkpointValue.toObject().value(QLatin1String("items")), sources.size(), &countedCheckpointPages);
        if (countedCheckpointPages > limits.maximumPageCount)
        {
            result.errorMessage = Helper::getCheckpointPagesText(limits.maximumPageCount);
            return result;
        }
        countedFilePages += countedCheckpointPages;
    }
    if (countedFilePages > limits.maximumFilePageCount)
    {
        result.errorMessage = Helper::getFilePagesText(limits.maximumFilePageCount);
        return result;
    }

    // The reading checks the limits again by each range (the count above has seen every text)
    qint64 itemPageTotal = 0;
    std::vector<PDFDocumentAssembly::Item> items;
    std::vector<std::pair<int, PDFDocumentAssembly::Item>> outOfRange;
    if (!Helper::readItems(itemsValue, sources, limits, limits.maximumPageCount, &itemPageTotal, &items, &outOfRange, &result.errorMessage))
    {
        return result;
    }
    qint64 pageTotal = itemPageTotal;

    // The pages over the page count of their source go to the bin (as by a changed source)
    std::vector<PDFDocumentAssembly::RemovedItem> removedItems;
    std::map<int, PDFInteger> removedPagesOfSource;
    QSet<QString> itemIds;
    for (const PDFDocumentAssembly::Item& item : items)
    {
        itemIds.insert(item.id);
    }
    for (auto& [itemIndex, removedItem] : outOfRange)
    {
        for (const PDFDocumentAssembly::Page& page : removedItem.pages)
        {
            ++removedPagesOfSource[page.plan.input];
        }
        PDFDocumentAssembly::RemovedItem binItem;
        if (itemIndex >= 0 || itemIds.contains(removedItem.id))
        {
            QString id;
            int number = 1;
            do
            {
                id = QStringLiteral("%1~%2").arg(removedItem.id).arg(number++);
            } while (itemIds.contains(id));
            removedItem.id = id;
        }
        itemIds.insert(removedItem.id);
        binItem.previousItemId = itemIndex >= 0 ? items[size_t(itemIndex)].id : QString();
        binItem.index = itemIndex >= 0 ? itemIndex + 1 : 0;
        binItem.item = std::move(removedItem);
        removedItems.push_back(std::move(binItem));
    }
    for (const auto& [input, count] : removedPagesOfSource)
    {
        const PDFDocumentAssembly::Source& source = sources[size_t(input)];
        result.problems.push_back({ source.id, tr("%Ln pages of %1 are not in the file and were moved to Removed pages.", nullptr, int(count)).arg(source.name) });
    }

    // The checkpoints (RB7: not in the assembly); a duplicate id is refused as the duplicate ids of
    // the items and of the sources (a restore names its checkpoint by the id, B23)
    std::vector<PDFDocumentAssembly::Checkpoint> checkpoints;
    if (!checkpointsValue.isUndefined())
    {
        if (!checkpointsValue.isArray() || checkpointsValue.toArray().size() > limits.maximumCheckpointCount)
        {
            result.errorMessage = tr("The assembly has more than %Ln checkpoints.", nullptr, limits.maximumCheckpointCount);
            return result;
        }
        QSet<QString> checkpointIds;
        for (const QJsonValue& checkpointValue : checkpointsValue.toArray())
        {
            const QJsonObject object = checkpointValue.toObject();
            PDFDocumentAssembly::Checkpoint checkpoint;
            QString time;
            if (!checkpointValue.isObject() || !Helper::readString(object, "id", limits.maximumNameLength, &checkpoint.id) || checkpoint.id.isEmpty() ||
                checkpointIds.contains(checkpoint.id) || !Helper::readString(object, "name", limits.maximumNameLength, &checkpoint.name) ||
                !Helper::readString(object, "time", 64, &time))
            {
                result.errorMessage = tr("A checkpoint of the assembly is not valid.");
                return result;
            }
            checkpointIds.insert(checkpoint.id);
            checkpoint.time = QDateTime::fromString(time, Qt::ISODateWithMs);

            std::vector<std::pair<int, PDFDocumentAssembly::Item>> checkpointOutOfRange;
            if (!Helper::readItems(object.value(QLatin1String("items")), sources, limits, limits.maximumFilePageCount, &pageTotal, &checkpoint.items, &checkpointOutOfRange, &result.errorMessage))
            {
                return result;
            }
            if (!checkpointOutOfRange.empty())
            {
                result.problems.push_back({ QString(), tr("The checkpoint %1 has pages, which are not in their files; they were left out.").arg(checkpoint.name) });
            }
            checkpoints.push_back(std::move(checkpoint));
        }
    }

    // The values of the output (only the known keys and plain values)
    QVariantMap outputParameters;
    const QJsonValue outputValue = root.value(QLatin1String("output"));
    if (outputValue.isObject())
    {
        const QJsonObject output = outputValue.toObject();
        for (const QString& key : Helper::getOutputKeys())
        {
            const QJsonValue value = output.value(key);
            if (value.isString() && value.toString().size() <= 32767)
            {
                outputParameters.insert(key, value.toString());
            }
        }
    }

    QString error;
    PDFDocumentAssembly assembly = PDFDocumentAssembly::create(std::move(sources), std::move(items), std::move(removedItems), &error);
    if (!error.isEmpty())
    {
        result.errorMessage = error;
        return result;
    }

    Content content;
    content.assembly = std::move(assembly);
    content.checkpoints = std::move(checkpoints);
    content.outputParameters = std::move(outputParameters);
    result.content = std::move(content);
    return result;
}

QByteArray PDFDocumentAssemblyFile::write(const Content& content, const QString& folder)
{
    using Helper = PDFDocumentAssemblyFileHelper;

    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(FORMAT_NAME));
    root.insert(QStringLiteral("version"), VERSION);

    QJsonArray sources;
    for (const PDFDocumentAssembly::Source& source : content.assembly.getSources())
    {
        QJsonObject object;
        object.insert(QStringLiteral("id"), source.id);
        object.insert(QStringLiteral("kind"), source.kind == PDFDocumentAssembly::SourceKind::Image ? QStringLiteral("image") : QStringLiteral("pdf"));
        object.insert(QStringLiteral("name"), source.name);
        if (!source.path.isEmpty())
        {
            object.insert(QStringLiteral("path"), QDir::fromNativeSeparators(source.path));
            object.insert(QStringLiteral("relativePath"), folder.isEmpty() ? source.relativePath : QDir(folder).relativeFilePath(source.path));
        }
        object.insert(QStringLiteral("size"), source.size);
        if (source.modified.isValid())
        {
            object.insert(QStringLiteral("modified"), source.modified.toUTC().toString(Qt::ISODateWithMs));
        }
        if (!source.sha256.isEmpty())
        {
            object.insert(QStringLiteral("sha256"), QString::fromLatin1(source.sha256.toHex()));
        }
        object.insert(QStringLiteral("pageCount"), qint64(source.pageCount));
        if (source.isParked)
        {
            object.insert(QStringLiteral("parked"), true);
        }
        if (source.kind == PDFDocumentAssembly::SourceKind::Image)
        {
            object.insert(QStringLiteral("format"), QString::fromLatin1(source.imageFormat));
            object.insert(QStringLiteral("data"), QString::fromLatin1(source.imageData.toBase64()));
        }
        sources.append(object);
    }
    root.insert(QStringLiteral("sources"), sources);
    root.insert(QStringLiteral("items"), Helper::writeItems(content.assembly.getItems()));

    QJsonArray checkpoints;
    for (const PDFDocumentAssembly::Checkpoint& checkpoint : content.checkpoints)
    {
        QJsonObject object;
        object.insert(QStringLiteral("id"), checkpoint.id);
        object.insert(QStringLiteral("name"), checkpoint.name);
        object.insert(QStringLiteral("time"), checkpoint.time.toUTC().toString(Qt::ISODateWithMs));
        object.insert(QStringLiteral("items"), Helper::writeItems(checkpoint.items));
        checkpoints.append(object);
    }
    root.insert(QStringLiteral("checkpoints"), checkpoints);

    QJsonObject output;
    for (const QString& key : Helper::getOutputKeys())
    {
        if (content.outputParameters.contains(key))
        {
            output.insert(key, content.outputParameters.value(key).toString());
        }
    }
    root.insert(QStringLiteral("output"), output);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool PDFDocumentAssemblyFile::canCreateCheckpoint(const PDFDocumentAssembly& assembly, const std::vector<PDFDocumentAssembly::Checkpoint>& checkpoints, QString* errorMessage)
{
    using Helper = PDFDocumentAssemblyFileHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (checkpoints.size() >= size_t(PDFDocumentAssembly::MAXIMUM_CHECKPOINT_COUNT))
    {
        error = tr("An assembly has at most %Ln checkpoints.", nullptr, PDFDocumentAssembly::MAXIMUM_CHECKPOINT_COUNT);
        return false;
    }

    // Owner 2026-11-12: the checkpoints leave the room of a whole assembly in the file, so the
    // items may grow to their limit later and the file is read again (MAXIMUM_CHECKPOINT_PAGE_COUNT)
    qint64 pageCount = Helper::getPageCount(assembly.getItems());
    if (pageCount > PDFDocumentAssembly::MAXIMUM_PAGE_COUNT)
    {
        // An assembly over its limit (the tab of a document, which has grown) has no checkpoint
        error = Helper::getCheckpointPagesText(PDFDocumentAssembly::MAXIMUM_PAGE_COUNT);
        return false;
    }
    for (const PDFDocumentAssembly::Checkpoint& checkpoint : checkpoints)
    {
        pageCount += Helper::getPageCount(checkpoint.items);
    }
    if (pageCount > MAXIMUM_CHECKPOINT_PAGE_COUNT)
    {
        error = tr("The checkpoints of an assembly have at most %Ln pages together.", nullptr, Helper::toCount(MAXIMUM_CHECKPOINT_PAGE_COUNT));
        return false;
    }
    return true;
}

bool PDFDocumentAssemblyFile::checkPageCount(const Content& content, QString* errorMessage)
{
    using Helper = PDFDocumentAssemblyFileHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    // The limits of read (the defaults), by the same texts
    const Limits limits;
    qint64 pageCount = Helper::getPageCount(content.assembly.getItems());
    if (pageCount > limits.maximumPageCount)
    {
        error = Helper::getItemPagesText(limits.maximumPageCount);
        return false;
    }
    for (const PDFDocumentAssembly::Checkpoint& checkpoint : content.checkpoints)
    {
        const qint64 checkpointPageCount = Helper::getPageCount(checkpoint.items);
        if (checkpointPageCount > limits.maximumPageCount)
        {
            error = Helper::getCheckpointPagesText(limits.maximumPageCount);
            return false;
        }
        pageCount += checkpointPageCount;
    }
    if (pageCount > limits.maximumFilePageCount)
    {
        error = Helper::getFilePagesText(limits.maximumFilePageCount);
        return false;
    }
    return true;
}

}   // namespace pdf
