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

#include "pdfdocumenttool_merge.h"

#include "pdfcatalog.h"
#include "pdfdocumenttool_compress.h"
#include "pdfdocumentwriter.h"
#include "pdfimagepagefactory.h"
#include "pdfoperationcontrol.h"

#include <QBuffer>
#include <QFileInfo>
#include <QLocale>

#include <cmath>
#include <set>

namespace pdf
{

/// Helpers of the tools Merge and Blank PDF
class PDFDocumentToolMergeHelper
{
public:
    PDFDocumentToolMergeHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// Longest letters of an input (more than enough for MAXIMUM_INPUT_COUNT, no overflow)
    static constexpr int MAXIMUM_LETTERS = 6;

    /// One parsed item of a plan
    struct PlanToken
    {
        int input = -1;
        PDFInteger firstPage = 0;   ///< Page number from 1
        PDFInteger lastPage = 0;    ///< Page number from 1
        PageRotation turn = PageRotation::None;
        bool isClone = false;
    };

    /// Reads a decimal number of at most 10 digits at \p position
    static bool readNumber(QStringView text, qsizetype* position, PDFInteger* value)
    {
        qsizetype end = *position;
        while (end < text.size() && text[end] >= QLatin1Char('0') && text[end] <= QLatin1Char('9') && end - *position < 10)
        {
            ++end;
        }
        if (end == *position || (end < text.size() && text[end].isDigit()))
        {
            return false;
        }

        bool ok = false;
        *value = text.mid(*position, end - *position).toLongLong(&ok);
        *position = end;
        return ok;
    }

    /// Parses "<letters><page>[-<page>][@<turn>][+]"
    static std::optional<PlanToken> parseToken(QStringView text)
    {
        PlanToken token;
        qsizetype position = 0;
        while (position < text.size() && text[position] >= QLatin1Char('A') && text[position] <= QLatin1Char('Z'))
        {
            ++position;
        }
        if (position == 0 || position > MAXIMUM_LETTERS)
        {
            return std::nullopt;
        }
        token.input = PDFDocumentToolMerge::getInputIndex(text.left(position));

        if (!readNumber(text, &position, &token.firstPage))
        {
            return std::nullopt;
        }
        token.lastPage = token.firstPage;

        if (position < text.size() && text[position] == QLatin1Char('-'))
        {
            ++position;
            if (!readNumber(text, &position, &token.lastPage))
            {
                return std::nullopt;
            }
        }

        if (position < text.size() && text[position] == QLatin1Char('@'))
        {
            ++position;
            PDFInteger angle = 0;
            if (!readNumber(text, &position, &angle))
            {
                return std::nullopt;
            }
            switch (angle)
            {
                case 0:
                    token.turn = PageRotation::None;
                    break;
                case 90:
                    token.turn = PageRotation::Rotate90;
                    break;
                case 180:
                    token.turn = PageRotation::Rotate180;
                    break;
                case 270:
                    token.turn = PageRotation::Rotate270;
                    break;
                default:
                    return std::nullopt;
            }
        }

        if (position < text.size() && text[position] == QLatin1Char('+'))
        {
            token.isClone = true;
            ++position;
        }

        if (position != text.size())
        {
            return std::nullopt;
        }
        return token;
    }

    /// Angle of a turn in a plan
    static int getAngle(PageRotation turn)
    {
        switch (turn)
        {
            case PageRotation::Rotate90:
                return 90;
            case PageRotation::Rotate180:
                return 180;
            case PageRotation::Rotate270:
                return 270;
            default:
                return 0;
        }
    }

    /// Text of an item of a plan
    static QString createToken(int input, PDFInteger firstPage, PDFInteger lastPage, PageRotation turn, bool isClone)
    {
        QString text = PDFDocumentToolMerge::getInputLetters(input) + QString::number(firstPage + 1);
        if (lastPage != firstPage)
        {
            text += QLatin1Char('-') + QString::number(lastPage + 1);
        }
        if (turn != PageRotation::None)
        {
            text += QLatin1Char('@') + QString::number(getAngle(turn));
        }
        if (isClone)
        {
            text += QLatin1Char('+');
        }
        return text;
    }

    /// Writes the document of a result into memory (the size is measured by writing it)
    static QByteArray writeDocument(const PDFDocument* document, QString* errorMessage)
    {
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QBuffer::WriteOnly);
        PDFDocumentWriter writer(nullptr);
        const PDFOperationResult result = writer.write(&buffer, document);
        buffer.close();
        if (!result)
        {
            *errorMessage = result.getErrorMessage();
            return QByteArray();
        }
        if (data.isEmpty())
        {
            *errorMessage = PDFDocumentToolMerge::tr("The result cannot be written.");
        }
        else if (data.size() > PDFDocumentTool::MAXIMUM_RESULT_SIZE)
        {
            *errorMessage = PDFDocumentToolMerge::tr("The result is too large to be saved.");
            return QByteArray();
        }
        return data;
    }

    /// A number of millimeters for a text: at most one decimal place, no trailing zero (K25)
    static QString formatMillimeters(double millimeters)
    {
        const double rounded = std::round(millimeters * 10.0) / 10.0;
        QString text = QLocale().toString(rounded, 'f', 1);
        const QString zero = QLocale().decimalPoint() + QLocale().zeroDigit();
        if (text.endsWith(zero))
        {
            text.chop(zero.size());
        }
        return text;
    }

    /// Millimeters of points
    static double getMillimeters(double points)
    {
        return points * 25.4 / 72.0;
    }

    /// The new document of a result with its written bytes
    static void addNewDocument(PDFDocumentToolResult* result, PDFDocument document, QByteArray writtenData, const QString& suggestedFileName)
    {
        PDFDocumentToolOutputDocument output;
        output.kind = PDFDocumentToolOutputDocument::Kind::New;
        output.inputIndex = 0;
        output.document = PDFDocumentPointer(new PDFDocument(std::move(document)));
        output.flags = PDFModifiedDocument::Reset;
        output.suggestedFileName = suggestedFileName;
        output.writtenData = std::move(writtenData);
        result->documents.push_back(std::move(output));
    }
};

// ------------------------------------------------------- PDFDocumentToolMerge

QString PDFDocumentToolMerge::getInputLetters(int inputIndex)
{
    // Bijective base 26: A ... Z, AA, AB, ... (R20)
    QString letters;
    if (inputIndex < 0)
    {
        return letters;
    }

    qint64 value = qint64(inputIndex) + 1;
    while (value > 0)
    {
        const qint64 remainder = (value - 1) % 26;
        letters.prepend(QLatin1Char(char('A' + remainder)));
        value = (value - 1) / 26;
    }
    return letters;
}

int PDFDocumentToolMerge::getInputIndex(QStringView letters)
{
    if (letters.isEmpty() || letters.size() > PDFDocumentToolMergeHelper::MAXIMUM_LETTERS)
    {
        return -1;
    }

    qint64 value = 0;
    for (const QChar character : letters)
    {
        if (character < QLatin1Char('A') || character > QLatin1Char('Z'))
        {
            return -1;
        }
        value = value * 26 + (character.unicode() - 'A' + 1);
    }
    return value - 1 <= std::numeric_limits<int>::max() ? int(value - 1) : -1;
}

std::optional<std::vector<PDFDocumentToolMerge::Input>> PDFDocumentToolMerge::resolveInputs(const PDFDocumentToolContext& context, const QVariantMap& parameters, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    const size_t total = context.documents.size() + context.images.size();
    if (total > size_t(MAXIMUM_INPUT_COUNT))
    {
        error = tr("Merge takes at most %Ln files.", nullptr, MAXIMUM_INPUT_COUNT);
        return std::nullopt;
    }

    auto createDocumentInput = [&context](size_t index)
    {
        const PDFDocumentToolInputDocument& document = context.documents[index];
        Input input;
        input.isImage = false;
        input.index = index;
        input.inputId = document.inputId;
        input.name = QFileInfo(document.fileName).fileName();
        input.pageCount = document.document ? PDFInteger(document.document->getCatalog()->getPageCount()) : 0;
        return input;
    };
    auto createImageInput = [&context](size_t index)
    {
        const PDFDocumentToolInputImage& image = context.images[index];
        Input input;
        input.isImage = true;
        input.index = index;
        input.inputId = image.inputId;
        input.name = QFileInfo(image.fileName).fileName();
        input.pageCount = PDFImagePageFactory::readInfo(image.data, image.format).frameCount;
        return input;
    };

    std::vector<Input> inputs;
    inputs.reserve(total);

    const QString order = parameters.value(PDFDocumentToolMergeHelper::name(PARAMETER_INPUTS)).toString().trimmed();
    if (order.isEmpty())
    {
        for (size_t i = 0; i < context.documents.size(); ++i)
        {
            inputs.push_back(createDocumentInput(i));
        }
        for (size_t i = 0; i < context.images.size(); ++i)
        {
            inputs.push_back(createImageInput(i));
        }
        return inputs;
    }

    std::set<QString> usedIds;
    std::vector<bool> usedDocuments(context.documents.size(), false);
    std::vector<bool> usedImages(context.images.size(), false);
    for (const QString& part : order.split(QLatin1Char(',')))
    {
        const QString id = part.trimmed();
        if (id.isEmpty())
        {
            continue;
        }
        if (!usedIds.insert(id).second)
        {
            error = tr("The input “%1” is in the order of the inputs more than once.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(id));
            return std::nullopt;
        }

        bool isFound = false;
        for (size_t i = 0; i < context.documents.size() && !isFound; ++i)
        {
            if (context.documents[i].inputId == id)
            {
                usedDocuments[i] = true;
                inputs.push_back(createDocumentInput(i));
                isFound = true;
            }
        }
        for (size_t i = 0; i < context.images.size() && !isFound; ++i)
        {
            if (context.images[i].inputId == id)
            {
                usedImages[i] = true;
                inputs.push_back(createImageInput(i));
                isFound = true;
            }
        }
        if (!isFound)
        {
            error = tr("The input “%1” is not among the files of the merge.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(id));
            return std::nullopt;
        }
    }

    // Every input of the context has its place: nothing is merged silently or left out silently
    if (std::find(usedDocuments.cbegin(), usedDocuments.cend(), false) != usedDocuments.cend() ||
        std::find(usedImages.cbegin(), usedImages.cend(), false) != usedImages.cend())
    {
        error = tr("A file of the merge is missing in the order of the inputs.");
        return std::nullopt;
    }
    return inputs;
}

std::optional<std::vector<PDFDocumentToolMerge::PlanItem>> PDFDocumentToolMerge::parsePlan(const QString& plan, const std::vector<PDFInteger>& pageCounts, QString* errorMessage)
{
    using Helper = PDFDocumentToolMergeHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (plan.size() > MAXIMUM_PLAN_LENGTH)
    {
        error = tr("The plan of the pages is longer than %1 characters.").arg(MAXIMUM_PLAN_LENGTH);
        return std::nullopt;
    }

    std::vector<PlanItem> items;
    std::set<std::pair<int, PDFInteger>> usedPages;
    const QStringView planView(plan);
    qsizetype start = 0;
    while (start <= planView.size())
    {
        qsizetype end = planView.indexOf(QLatin1Char(','), start);
        if (end < 0)
        {
            end = planView.size();
        }

        const QStringView part = planView.mid(start, end - start).trimmed();
        start = end + 1;
        if (part.isEmpty())
        {
            continue;
        }

        const std::optional<Helper::PlanToken> token = Helper::parseToken(part);
        if (!token)
        {
            error = tr("“%1” is not a part of a plan of the pages. Write the file and its pages, such as A1-6, B3@90 or A3+ (a turn @90, @180, @270; + a copy of a page).")
                        .arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(part.toString()));
            return std::nullopt;
        }
        if (token->input < 0 || size_t(token->input) >= pageCounts.size())
        {
            error = tr("The plan of the pages names the file %1, which is not in the list.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(part.toString()));
            return std::nullopt;
        }

        const PDFInteger pageCount = pageCounts[size_t(token->input)];
        if (token->firstPage < 1 || token->lastPage < 1 || token->firstPage > pageCount || token->lastPage > pageCount)
        {
            error = tr("The file %1 has no page %2 (it has %Ln pages).", nullptr, int(qMin<PDFInteger>(pageCount, std::numeric_limits<int>::max())))
                        .arg(getInputLetters(token->input))
                        .arg(token->firstPage > pageCount || token->firstPage < 1 ? token->firstPage : token->lastPage);
            return std::nullopt;
        }

        const PDFInteger step = token->lastPage >= token->firstPage ? 1 : -1;
        const PDFInteger count = (token->lastPage - token->firstPage) * step + 1;
        if (PDFInteger(items.size()) + count > PDFDocumentMerger::MAXIMUM_PAGE_COUNT)
        {
            error = tr("The plan of the pages has more than %Ln pages.", nullptr, int(PDFDocumentMerger::MAXIMUM_PAGE_COUNT));
            return std::nullopt;
        }

        for (PDFInteger page = token->firstPage - 1; ; page += step)
        {
            PlanItem item;
            item.input = token->input;
            item.pageIndex = page;
            item.turn = token->turn;
            item.isClone = !usedPages.emplace(token->input, page).second;
            if (item.isClone && !token->isClone)
            {
                error = tr("The page %1 of the file %2 is in the plan of the pages more than once. Mark each further copy of it by +, such as %2%1+.")
                            .arg(page + 1)
                            .arg(getInputLetters(token->input));
                return std::nullopt;
            }
            items.push_back(item);
            if (page == token->lastPage - 1)
            {
                break;
            }
        }
    }

    if (items.empty())
    {
        error = tr("The plan of the pages has no page.");
        return std::nullopt;
    }
    return items;
}

QString PDFDocumentToolMerge::createPlan(const std::vector<PlanItem>& items)
{
    QStringList parts;
    for (size_t i = 0; i < items.size();)
    {
        const PlanItem& first = items[i];
        size_t last = i;
        PDFInteger step = 0;
        if (i + 1 < items.size())
        {
            const PlanItem& next = items[i + 1];
            const PDFInteger difference = next.pageIndex - first.pageIndex;
            if (next.input == first.input && next.turn == first.turn && next.isClone == first.isClone && (difference == 1 || difference == -1))
            {
                step = difference;
            }
        }
        if (step != 0)
        {
            while (last + 1 < items.size() &&
                   items[last + 1].input == first.input &&
                   items[last + 1].turn == first.turn &&
                   items[last + 1].isClone == first.isClone &&
                   items[last + 1].pageIndex == items[last].pageIndex + step)
            {
                ++last;
            }
        }
        parts << PDFDocumentToolMergeHelper::createToken(first.input, first.pageIndex, items[last].pageIndex, first.turn, first.isClone);
        i = last + 1;
    }
    return parts.join(QLatin1Char(','));
}

std::vector<PDFDocumentToolMerge::PlanItem> PDFDocumentToolMerge::createDefaultPlan(const std::vector<PDFInteger>& pageCounts)
{
    std::vector<PlanItem> items;
    for (size_t input = 0; input < pageCounts.size(); ++input)
    {
        for (PDFInteger page = 0; page < pageCounts[input]; ++page)
        {
            PlanItem item;
            item.input = int(input);
            item.pageIndex = page;
            items.push_back(item);
        }
    }
    return items;
}

PDFDocumentMerger::Settings PDFDocumentToolMerge::createSettings(const QVariantMap& parameters)
{
    PDFDocumentMerger::Settings settings;
    const QString outline = parameters.value(PDFDocumentToolMergeHelper::name(PARAMETER_OUTLINE)).toString();
    if (outline == QLatin1String(OUTLINE_JOIN))
    {
        settings.outline = PDFDocumentMerger::OutlineMode::Join;
    }
    else if (outline == QLatin1String(OUTLINE_NONE))
    {
        settings.outline = PDFDocumentMerger::OutlineMode::NoOutline;
    }
    else
    {
        settings.outline = PDFDocumentMerger::OutlineMode::DocumentParts;
    }
    settings.checkPermissions = true;
    return settings;
}

std::optional<PDFImageOptimizer::Settings> PDFDocumentToolMerge::createImageSettings(const QVariantMap& parameters)
{
    // One definition with Compress (K24, R19): the images of its presets
    const QString optimize = parameters.value(PDFDocumentToolMergeHelper::name(PARAMETER_OPTIMIZE_IMAGES)).toString();
    QVariantMap compressParameters;
    if (optimize == QLatin1String(OPTIMIZE_PREFER_QUALITY))
    {
        compressParameters.insert(QString::fromLatin1(PDFDocumentToolCompress::PARAMETER_PRESET), QString::fromLatin1(PDFDocumentToolCompress::PRESET_BALANCED));
    }
    else if (optimize == QLatin1String(OPTIMIZE_MINIMUM_SIZE))
    {
        compressParameters.insert(QString::fromLatin1(PDFDocumentToolCompress::PARAMETER_PRESET), QString::fromLatin1(PDFDocumentToolCompress::PRESET_SMALLER_FILE));
    }
    else
    {
        return std::nullopt;
    }
    return PDFDocumentToolCompress::createSettings(compressParameters).images;
}

QString PDFDocumentToolMerge::getSuggestedFileName(const QString& firstInputName)
{
    const QString baseName = QFileInfo(firstInputName).completeBaseName();
    return baseName.isEmpty() ? QStringLiteral("merged.pdf") : baseName + QStringLiteral("_merged.pdf");
}

QString PDFDocumentToolMerge::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolMerge::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolMergeHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_INPUTS), QT_TR_NOOP("Files"),
                                                   QT_TR_NOOP("Ids of the files in their order, separated by commas; the first is the file A"),
                                                   QString(), MAXIMUM_INPUTS_LENGTH).setStored(false).setPanelOnly(true));
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_PLAN), QT_TR_NOOP("Pages"),
                                                   QT_TR_NOOP("Pages of the result in their order, such as \"A1-6,B1-9,C1,A3@90,A3+\"; empty = every page of every file"),
                                                   QString()).setStored(false).setPanelOnly(true).setMaximumLength(MAXIMUM_PLAN_LENGTH));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_OUTLINE), QT_TR_NOOP("Outline"), QT_TR_NOOP("Bookmarks of the result"),
                                                 {
                                                     { QString::fromLatin1(OUTLINE_DOCUMENT_PARTS), QT_TR_NOOP("Document parts"), QT_TR_NOOP("One bookmark per source file") },
                                                     { QString::fromLatin1(OUTLINE_JOIN), QT_TR_NOOP("Join outlines"), QT_TR_NOOP("Bookmarks of the files nested under each part") },
                                                     { QString::fromLatin1(OUTLINE_NONE), QT_TR_NOOP("No outline"), QT_TR_NOOP("The result has no bookmarks") },
                                                 },
                                                 QString::fromLatin1(OUTLINE_DOCUMENT_PARTS)));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_OPTIMIZE_IMAGES), QT_TR_NOOP("Optimize"), QT_TR_NOOP("Images of the result"),
                                                 {
                                                     { QString::fromLatin1(OPTIMIZE_OFF), QT_TR_NOOP("Off"), QT_TR_NOOP("Images are stored as they are") },
                                                     { QString::fromLatin1(OPTIMIZE_PREFER_QUALITY), QT_TR_NOOP("Prefer quality"), QT_TR_NOOP("Images are reduced to 200 dpi, JPEG quality 80") },
                                                     { QString::fromLatin1(OPTIMIZE_MINIMUM_SIZE), QT_TR_NOOP("Minimum size"), QT_TR_NOOP("Images are reduced to 110 dpi, JPEG quality 60") },
                                                 },
                                                 QString::fromLatin1(OPTIMIZE_OFF)));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolMerge::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolMergeHelper;

    // An assistant gives the documents by documentIds, their order is the order of the
    // result (owner answer Q11): the inputs and the plan belong to the panels
    std::vector<PDFDocumentToolParameterError> errors;
    for (const char* parameter : { PARAMETER_INPUTS, PARAMETER_PLAN })
    {
        if (given.contains(Helper::name(parameter)))
        {
            errors.push_back({ Helper::name(parameter), tr("%1 is not a parameter of an assistant: give the documents by documentIds, in the order of the result.").arg(Helper::name(parameter)) });
        }
    }
    return errors;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolMerge::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolMergeHelper;

    // Not the default validation: a merge of images only has no document
    std::vector<PDFDocumentToolParameterError> errors;
    for (const PDFDocumentToolInputDocument& document : context.documents)
    {
        if (!document.document)
        {
            errors.push_back({ QString(), tr("An input document is missing.") });
            return errors;
        }
    }
    for (const PDFDocumentToolInputImage& image : context.images)
    {
        if (image.data.isEmpty())
        {
            errors.push_back({ QString(), tr("An input image has no data.") });
            return errors;
        }
    }

    QString error;
    const std::optional<std::vector<Input>> inputs = resolveInputs(context, parameters, &error);
    if (!inputs)
    {
        errors.push_back({ Helper::name(PARAMETER_INPUTS), error });
        return errors;
    }
    if (inputs->size() < size_t(MINIMUM_INPUT_COUNT))
    {
        errors.push_back({ QString(), tr("Merge needs at least two files.") });
        return errors;
    }

    std::vector<PDFInteger> pageCounts;
    for (const Input& input : *inputs)
    {
        if (input.pageCount <= 0)
        {
            const QString reason = input.isImage ? PDFImagePageFactory::readInfo(context.images[input.index].data, context.images[input.index].format).error : tr("The document has no pages.");
            errors.push_back({ QString(), tr("%1 cannot be merged: %2").arg(input.name.isEmpty() ? getInputLetters(int(pageCounts.size())) : input.name, reason) });
            return errors;
        }
        if (!input.isImage)
        {
            const PDFDocumentToolInputDocument& document = context.documents[input.index];
            const PDFDocumentMerger::Permission permission = PDFDocumentMerger::getPermission(document.document.data(), document.isOwnerPasswordVerified);
            if (permission != PDFDocumentMerger::Permission::Permitted)
            {
                errors.push_back({ QString(), PDFDocumentMerger::getPermissionText(permission, input.name) });
                return errors;
            }
        }
        pageCounts.push_back(input.pageCount);
    }

    const QString plan = parameters.value(Helper::name(PARAMETER_PLAN)).toString();
    if (!plan.trimmed().isEmpty() && !parsePlan(plan, pageCounts, &error))
    {
        errors.push_back({ Helper::name(PARAMETER_PLAN), error });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolMerge::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolMergeHelper;

    QString error;
    const std::optional<std::vector<Input>> inputs = resolveInputs(context, parameters, &error);
    if (!inputs)
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    std::vector<PDFInteger> pageCounts;
    std::vector<PDFDocumentMerger::Source> sources;
    for (const Input& input : *inputs)
    {
        pageCounts.push_back(input.pageCount);
        if (input.isImage)
        {
            const PDFDocumentToolInputImage& image = context.images[input.index];
            sources.push_back(PDFDocumentMerger::Source::createImage(image.data, image.format, image.dotsPerInch, input.name));
        }
        else
        {
            PDFDocumentMerger::Source source = PDFDocumentMerger::Source::createDocument(context.documents[input.index].document, input.name);
            source.isOwnerPasswordVerified = context.documents[input.index].isOwnerPasswordVerified;
            sources.push_back(std::move(source));
        }
    }

    const QString planText = parameters.value(Helper::name(PARAMETER_PLAN)).toString();
    std::optional<std::vector<PlanItem>> plan = planText.trimmed().isEmpty() ? createDefaultPlan(pageCounts) : parsePlan(planText, pageCounts, &error);
    if (!plan)
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    std::vector<PDFDocumentMerger::Item> items;
    items.reserve(plan->size());
    for (const PlanItem& planItem : *plan)
    {
        PDFDocumentMerger::Item item;
        item.source = size_t(planItem.input);
        item.pageIndex = planItem.pageIndex;
        item.turn = planItem.turn;
        items.push_back(item);
    }

    const std::optional<PDFImageOptimizer::Settings> imageSettings = createImageSettings(parameters);

    PDFProgressPhases phases(progress);
    const int mergePhase = phases.addPhase(70, tr("Joining the files"), true);
    const int imagesPhase = imageSettings ? phases.addPhase(20, tr("Optimizing the images"), true) : -1;
    const int measurePhase = phases.addPhase(10, tr("Measuring the result"), false);
    phases.start();

    phases.beginPhase(mergePhase);
    PDFDocumentMerger::Result merged = PDFDocumentMerger::merge(sources, items, createSettings(parameters), control, phases.getPhase(mergePhase));
    if (merged.isCanceled || PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }
    if (!merged.isSuccess)
    {
        return PDFDocumentToolResult::createFailed(merged.errorMessage);
    }

    PDFDocument output = std::move(merged.document);
    qint64 imagesRecompressed = 0;
    if (imageSettings)
    {
        phases.beginPhase(imagesPhase);
        std::vector<PDFImageOptimizer::ImageResult> imageResults;
        PDFImageOptimizer imageOptimizer;
        output = imageOptimizer.optimize(&output, *imageSettings, PDFImageOptimizer::ImageOverrides(), phases.getPhase(imagesPhase), nullptr, &imageResults, control, nullptr);
        if (PDFOperationControl::isOperationCancelled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }
        for (const PDFImageOptimizer::ImageResult& imageResult : imageResults)
        {
            imagesRecompressed += imageResult.keptOriginal ? 0 : 1;
        }
    }

    phases.beginPhase(measurePhase);
    QByteArray writtenData = Helper::writeDocument(&output, &error);
    if (writtenData.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(error);
    }
    phases.finish();

    const PDFDocumentMerger::Statistics& statistics = merged.statistics;
    PDFDocumentToolResult result;

    result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), tr("%1 from %Ln files", nullptr, int(statistics.inputCount)).arg(statistics.pageCount) });
    QString outlineText;
    if (statistics.isSingleDocument && statistics.inputCount <= 1)
    {
        // One document without images keeps its outline (R3); with images the mode applies (Q18)
        outlineText = tr("Kept from the document");
    }
    else
    {
        switch (createSettings(parameters).outline)
        {
            case PDFDocumentMerger::OutlineMode::DocumentParts:
                outlineText = tr("%Ln document parts", nullptr, int(statistics.partCount));
                break;
            case PDFDocumentMerger::OutlineMode::Join:
                outlineText = tr("%Ln document parts with their bookmarks", nullptr, int(statistics.partCount));
                break;
            case PDFDocumentMerger::OutlineMode::NoOutline:
                outlineText = tr("None");
                break;
        }
    }
    result.facts.push_back({ QStringLiteral("outline"), tr("Outline"), outlineText });
    result.facts.push_back({ QStringLiteral("size"), tr("Size"), PDFDocumentToolFormat::formatSize(writtenData.size()) });
    if (statistics.fieldsRenamed > 0)
    {
        result.facts.push_back({ QStringLiteral("fieldsRenamed"), tr("Form fields"), tr("%Ln form fields renamed", nullptr, int(statistics.fieldsRenamed)) });
    }
    if (statistics.destinationsRenamed > 0)
    {
        result.facts.push_back({ QStringLiteral("destinationsRenamed"), tr("Destinations"), tr("%Ln named destinations renamed", nullptr, int(statistics.destinationsRenamed)) });
    }
    if (statistics.signaturesRemoved > 0)
    {
        result.facts.push_back({ QStringLiteral("signatures"), tr("Signatures"), tr("%Ln signatures removed", nullptr, int(statistics.signaturesRemoved)) });
    }
    if (statistics.structureTreeRemoved)
    {
        result.facts.push_back({ QStringLiteral("tags"), tr("Tags"), tr("Removed: not every file is tagged and taken whole") });
    }
    else if (statistics.structureTreeMerged)
    {
        result.facts.push_back({ QStringLiteral("tags"), tr("Tags"), tr("Joined") });
    }
    // The images are a fact only when Optimize was on: without it the dialog shows Pages,
    // Outline and Size (378:3674, 412:32654)
    if (imageSettings)
    {
        result.facts.push_back({ QStringLiteral("images"), tr("Images"), tr("%Ln recompressed", nullptr, int(imagesRecompressed)) });
    }

    if (statistics.scriptsRemoved)
    {
        result.steps.push_back({ QString(), tr("Scripts and actions of the documents removed"), PDFDocumentToolStep::State::Done });
    }
    if (statistics.xfaRemoved)
    {
        result.steps.push_back({ QString(), tr("XFA forms removed, the form fields stay"), PDFDocumentToolStep::State::Done });
    }
    if (statistics.collectionRemoved)
    {
        result.steps.push_back({ QString(), tr("Portfolio view removed, the attachments stay"), PDFDocumentToolStep::State::Done });
    }
    if (statistics.layerConfigurationsRemoved)
    {
        result.steps.push_back({ QString(), tr("Alternate configurations of the layers removed"), PDFDocumentToolStep::State::Done });
    }
    if (statistics.outputIntentsRemoved)
    {
        result.steps.push_back({ QString(), tr("The files have different output intents: the result has none"), PDFDocumentToolStep::State::Warning });
    }
    addConformanceFact(&result, statistics.conformanceRemoved);

    // A partial result (owner answer Q5): the inputs, whose copy failed
    for (const PDFDocumentMerger::LeftOut& leftOut : merged.leftOut)
    {
        const Input& input = (*inputs)[leftOut.source];
        PDFDocumentToolLeftOutInput leftOutInput;
        leftOutInput.inputId = input.inputId;
        leftOutInput.inputIndex = int(leftOut.source);
        leftOutInput.name = input.name;
        leftOutInput.pageCount = leftOut.pageCount;
        leftOutInput.reason = leftOut.reason;
        result.leftOutInputs.push_back(std::move(leftOutInput));
    }
    if (!result.leftOutInputs.empty())
    {
        QStringList names;
        for (const PDFDocumentToolLeftOutInput& leftOut : result.leftOutInputs)
        {
            names << tr("%1 · %Ln pages", nullptr, int(leftOut.pageCount)).arg(leftOut.name);
        }
        result.facts.push_back({ QStringLiteral("leftOut"), tr("Left out"), names.join(QStringLiteral(", ")) });
    }

    result.data.insert(QStringLiteral("pageCount"), statistics.pageCount);
    result.data.insert(QStringLiteral("inputCount"), statistics.inputCount);
    result.data.insert(QStringLiteral("partCount"), statistics.partCount);
    result.data.insert(QStringLiteral("outline"), parameters.value(Helper::name(PARAMETER_OUTLINE)).toString());
    result.data.insert(QStringLiteral("optimizeImages"), parameters.value(Helper::name(PARAMETER_OPTIMIZE_IMAGES)).toString());
    result.data.insert(QStringLiteral("imagesRecompressed"), imagesRecompressed);
    result.data.insert(QStringLiteral("sizeAfter"), qint64(writtenData.size()));
    result.data.insert(QStringLiteral("fieldsRenamed"), statistics.fieldsRenamed);
    result.data.insert(QStringLiteral("destinationsRenamed"), statistics.destinationsRenamed);
    result.data.insert(QStringLiteral("namesRenamed"), statistics.namesRenamed);
    result.data.insert(QStringLiteral("signaturesRemoved"), statistics.signaturesRemoved);
    result.data.insert(QStringLiteral("structureTreeRemoved"), statistics.structureTreeRemoved);
    result.data.insert(QStringLiteral("structureTreeMerged"), statistics.structureTreeMerged);
    result.data.insert(QStringLiteral("scriptsRemoved"), statistics.scriptsRemoved);
    result.data.insert(QStringLiteral("leftOutCount"), qint64(result.leftOutInputs.size()));
    result.data.insert(QStringLiteral("signatureCount"), 0);
    result.data.insert(QStringLiteral("signatureImpact"), QStringLiteral("none"));

    Helper::addNewDocument(&result, std::move(output), std::move(writtenData), getSuggestedFileName(inputs->empty() ? QString() : inputs->front().name));
    return result;
}

// ------------------------------------------------------- PDFDocumentToolBlank

std::optional<QSizeF> PDFDocumentToolBlank::getNamedSize(const QString& sizeId)
{
    // ISO 216 in millimeters, the US sizes and 16:9 in points (5.4 of the plan)
    if (sizeId == QLatin1String(SIZE_A4))
    {
        return QSizeF(getPoints(210.0), getPoints(297.0));
    }
    if (sizeId == QLatin1String(SIZE_A3))
    {
        return QSizeF(getPoints(297.0), getPoints(420.0));
    }
    if (sizeId == QLatin1String(SIZE_A5))
    {
        return QSizeF(getPoints(148.0), getPoints(210.0));
    }
    if (sizeId == QLatin1String(SIZE_LETTER))
    {
        return QSizeF(612.0, 792.0);
    }
    if (sizeId == QLatin1String(SIZE_LEGAL))
    {
        return QSizeF(612.0, 1008.0);
    }
    if (sizeId == QLatin1String(SIZE_16X9))
    {
        return QSizeF(540.0, 960.0);
    }
    return std::nullopt;
}

double PDFDocumentToolBlank::getPoints(double millimeters)
{
    return millimeters * 72.0 / 25.4;
}

bool PDFDocumentToolBlank::isValidSide(double points)
{
    // A limit given in millimeters (the definitions of width and height) is the limit in points
    // up to the rounding of the conversion
    static constexpr double TOLERANCE = 1e-9;
    return std::isfinite(points) && points >= MINIMUM_SIDE_POINTS - TOLERANCE && points <= MAXIMUM_SIDE_POINTS + TOLERANCE;
}

QSizeF PDFDocumentToolBlank::getPageSize(const QVariantMap& parameters, QString* errorMessage)
{
    using Helper = PDFDocumentToolMergeHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    const QString sizeId = parameters.value(Helper::name(PARAMETER_PAGE_SIZE)).toString();
    if (sizeId == QLatin1String(SIZE_CUSTOM))
    {
        const double width = getPoints(parameters.value(Helper::name(PARAMETER_WIDTH)).toDouble());
        const double height = getPoints(parameters.value(Helper::name(PARAMETER_HEIGHT)).toDouble());
        if (!isValidSide(width) || !isValidSide(height))
        {
            error = tr("The width and the height of a page must be between %1 and %2 mm.")
                        .arg(Helper::formatMillimeters(MINIMUM_SIDE_MILLIMETERS), Helper::formatMillimeters(MAXIMUM_SIDE_MILLIMETERS));
            return QSizeF();
        }
        return QSizeF(width, height);
    }

    const std::optional<QSizeF> size = getNamedSize(sizeId);
    if (!size)
    {
        error = tr("Unknown page size.");
        return QSizeF();
    }

    const bool isLandscape = parameters.value(Helper::name(PARAMETER_ORIENTATION)).toString() == QLatin1String(ORIENTATION_LANDSCAPE);
    return isLandscape ? size->transposed() : *size;
}

QString PDFDocumentToolBlank::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolBlank::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolMergeHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createInteger(tr, Helper::name(PARAMETER_PAGE_COUNT), QT_TR_NOOP("Pages"), QT_TR_NOOP("Number of the blank pages"),
                                                    DEFAULT_PAGE_COUNT, MINIMUM_PAGE_COUNT, MAXIMUM_PAGE_COUNT));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_PAGE_SIZE), QT_TR_NOOP("Size"), QT_TR_NOOP("Size of every page of the new document"),
                                                 {
                                                     { QString::fromLatin1(SIZE_A4), QT_TR_NOOP("A4"), QT_TR_NOOP("210 × 297 mm") },
                                                     { QString::fromLatin1(SIZE_A3), QT_TR_NOOP("A3"), QT_TR_NOOP("297 × 420 mm") },
                                                     { QString::fromLatin1(SIZE_A5), QT_TR_NOOP("A5"), QT_TR_NOOP("148 × 210 mm") },
                                                     { QString::fromLatin1(SIZE_LETTER), QT_TR_NOOP("Letter"), QT_TR_NOOP("216 × 279 mm") },
                                                     { QString::fromLatin1(SIZE_LEGAL), QT_TR_NOOP("Legal"), QT_TR_NOOP("216 × 356 mm") },
                                                     { QString::fromLatin1(SIZE_16X9), QT_TR_NOOP("16:9"), QT_TR_NOOP("Presentation · 338.7 × 190.5 mm") },
                                                     { QString::fromLatin1(SIZE_CUSTOM), QT_TR_NOOP("Custom"), QT_TR_NOOP("Width and height fields appear in the toolbar") },
                                                 },
                                                 QString::fromLatin1(SIZE_A4)));
    definitions.push_back(Definition::createReal(tr, Helper::name(PARAMETER_WIDTH), QT_TR_NOOP("Width"), QT_TR_NOOP("Width of a page of the custom size, in millimeters"),
                                                 DEFAULT_WIDTH_MILLIMETERS, MINIMUM_SIDE_MILLIMETERS, MAXIMUM_SIDE_MILLIMETERS));
    definitions.push_back(Definition::createReal(tr, Helper::name(PARAMETER_HEIGHT), QT_TR_NOOP("Height"), QT_TR_NOOP("Height of a page of the custom size, in millimeters"),
                                                 DEFAULT_HEIGHT_MILLIMETERS, MINIMUM_SIDE_MILLIMETERS, MAXIMUM_SIDE_MILLIMETERS));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_ORIENTATION), QT_TR_NOOP("Orientation"), QT_TR_NOOP("Orientation of a page of a named size; omitted: landscape for 16:9, else portrait (a custom size has the orientation of its width and height)"),
                                                 {
                                                     { QString::fromLatin1(ORIENTATION_PORTRAIT), QT_TR_NOOP("Portrait"), QT_TR_NOOP("Higher than wide") },
                                                     { QString::fromLatin1(ORIENTATION_LANDSCAPE), QT_TR_NOOP("Landscape"), QT_TR_NOOP("Wider than high") },
                                                 },
                                                 QString::fromLatin1(ORIENTATION_PORTRAIT)));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolBlank::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolMergeHelper;

    // width and height belong to the custom size, the orientation to a named one: a value,
    // which would be ignored, is an error, which says what to set
    std::vector<PDFDocumentToolParameterError> errors;
    const bool isCustom = given.value(Helper::name(PARAMETER_PAGE_SIZE)).toString() == QLatin1String(SIZE_CUSTOM);
    if (isCustom)
    {
        if (given.contains(Helper::name(PARAMETER_ORIENTATION)))
        {
            errors.push_back({ Helper::name(PARAMETER_ORIENTATION), tr("orientation is not used with pageSize custom: width and height give it. Leave orientation out.") });
        }
        for (const char* parameter : { PARAMETER_WIDTH, PARAMETER_HEIGHT })
        {
            if (!given.contains(Helper::name(parameter)))
            {
                errors.push_back({ Helper::name(parameter), tr("pageSize custom needs width and height in millimeters.") });
                break;
            }
        }
    }
    else
    {
        for (const char* parameter : { PARAMETER_WIDTH, PARAMETER_HEIGHT })
        {
            if (given.contains(Helper::name(parameter)))
            {
                errors.push_back({ Helper::name(parameter), tr("%1 is used only with pageSize custom. Set pageSize to custom, or leave %1 out.").arg(Helper::name(parameter)) });
            }
        }
    }
    return errors;
}

QVariantMap PDFDocumentToolBlank::completeGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolMergeHelper;

    // Owner answer Q22 (as the GUI, Q14): a presentation page without an orientation is on its
    // side (960 x 540); a given orientation stays, the other sizes keep the default portrait
    QVariantMap completed = given;
    const bool isPresentation = given.value(Helper::name(PARAMETER_PAGE_SIZE)).toString() == QLatin1String(SIZE_16X9);
    if (isPresentation && !given.contains(Helper::name(PARAMETER_ORIENTATION)))
    {
        completed.insert(Helper::name(PARAMETER_ORIENTATION), QString::fromLatin1(ORIENTATION_LANDSCAPE));
    }
    return completed;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolBlank::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    QString error;
    if (getPageSize(parameters, &error).isEmpty())
    {
        const bool isCustom = parameters.value(PDFDocumentToolMergeHelper::name(PARAMETER_PAGE_SIZE)).toString() == QLatin1String(SIZE_CUSTOM);
        errors.push_back({ PDFDocumentToolMergeHelper::name(isCustom ? PARAMETER_WIDTH : PARAMETER_PAGE_SIZE), error });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolBlank::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(context);
    using Helper = PDFDocumentToolMergeHelper;

    QString error;
    const QSizeF pageSize = getPageSize(parameters, &error);
    if (pageSize.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    const qint64 pageCount = parameters.value(Helper::name(PARAMETER_PAGE_COUNT)).toLongLong();
    if (pageCount < MINIMUM_PAGE_COUNT || pageCount > MAXIMUM_PAGE_COUNT)
    {
        return PDFDocumentToolResult::createFailed(tr("The number of the pages must be between %1 and %2.").arg(MINIMUM_PAGE_COUNT).arg(MAXIMUM_PAGE_COUNT));
    }

    std::vector<PDFDocumentMerger::Source> sources = { PDFDocumentMerger::Source::createBlank() };
    std::vector<PDFDocumentMerger::Item> items;
    items.resize(size_t(pageCount));
    for (PDFDocumentMerger::Item& item : items)
    {
        item.source = 0;
        item.pageSize = pageSize;
    }

    PDFDocumentMerger::Settings settings;
    settings.outline = PDFDocumentMerger::OutlineMode::NoOutline;
    PDFDocumentMerger::Result merged = PDFDocumentMerger::merge(sources, items, settings, control, progress);
    if (merged.isCanceled || PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }
    if (!merged.isSuccess)
    {
        return PDFDocumentToolResult::createFailed(merged.errorMessage);
    }

    QByteArray writtenData = Helper::writeDocument(&merged.document, &error);
    if (writtenData.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    const QString sizeId = parameters.value(Helper::name(PARAMETER_PAGE_SIZE)).toString();
    const bool isLandscape = pageSize.width() > pageSize.height();
    const QString orientation = QString::fromLatin1(isLandscape ? ORIENTATION_LANDSCAPE : ORIENTATION_PORTRAIT);
    const QString dimensions = tr("%1 × %2 mm").arg(Helper::formatMillimeters(Helper::getMillimeters(pageSize.width())), Helper::formatMillimeters(Helper::getMillimeters(pageSize.height())));

    PDFDocumentToolResult result;
    result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), tr("%Ln blank pages", nullptr, int(pageCount)) });
    result.facts.push_back({ QStringLiteral("pageSize"), tr("Page size"), dimensions });
    result.data.insert(QStringLiteral("pageCount"), pageCount);
    result.data.insert(QStringLiteral("pageSize"), sizeId);
    result.data.insert(QStringLiteral("widthPoints"), pageSize.width());
    result.data.insert(QStringLiteral("heightPoints"), pageSize.height());
    result.data.insert(QStringLiteral("orientation"), orientation);
    result.data.insert(QStringLiteral("sizeAfter"), qint64(writtenData.size()));
    Helper::addNewDocument(&result, std::move(merged.document), std::move(writtenData), QStringLiteral("Untitled.pdf"));
    return result;
}

}   // namespace pdf
