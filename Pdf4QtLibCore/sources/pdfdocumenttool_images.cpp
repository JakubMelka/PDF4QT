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

#include "pdfdocumenttool_images.h"

#include "pdfcatalog.h"
#include "pdfdocumenttool_geometry.h"
#include "pdfdocumenttool_merge.h"
#include "pdfoperationcontrol.h"
#include "pdfoutline.h"

#include <QFileInfo>
#include <QStringList>

#include <cmath>
#include <limits>
#include <set>

namespace pdf
{

/// Helpers of the tools Images to PDF and Interleave Pages
class PDFDocumentToolImagesHelper
{
public:
    PDFDocumentToolImagesHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// Reads a whole decimal number of at most 9 digits at \p position
    static bool readNumber(QStringView text, qsizetype* position, int* value)
    {
        qsizetype end = *position;
        while (end < text.size() && text[end] >= QLatin1Char('0') && text[end] <= QLatin1Char('9') && end - *position < 9)
        {
            ++end;
        }
        if (end == *position || (end < text.size() && text[end].isDigit()))
        {
            return false;
        }

        bool ok = false;
        *value = text.mid(*position, end - *position).toInt(&ok);
        *position = end;
        return ok;
    }

    /// Parses one item of the adjustments "<letters>[@<turn>][#<left>,<top>,<right>,<bottom>]"
    static std::optional<std::pair<int, PDFDocumentToolImagesToPdf::Adjustment>> parseAdjustment(QStringView text)
    {
        qsizetype position = 0;
        while (position < text.size() && text[position] >= QLatin1Char('A') && text[position] <= QLatin1Char('Z'))
        {
            ++position;
        }
        const int input = PDFDocumentPagePlan::getInputIndex(text.left(position));
        if (input < 0)
        {
            return std::nullopt;
        }

        PDFDocumentToolImagesToPdf::Adjustment adjustment;
        if (position < text.size() && text[position] == QLatin1Char('@'))
        {
            ++position;
            int angle = 0;
            if (!readNumber(text, &position, &angle))
            {
                return std::nullopt;
            }
            switch (angle)
            {
                case 0:
                    adjustment.turn = PageRotation::None;
                    break;
                case 90:
                    adjustment.turn = PageRotation::Rotate90;
                    break;
                case 180:
                    adjustment.turn = PageRotation::Rotate180;
                    break;
                case 270:
                    adjustment.turn = PageRotation::Rotate270;
                    break;
                default:
                    return std::nullopt;
            }
        }

        if (position < text.size() && text[position] == QLatin1Char('#'))
        {
            ++position;
            int values[4] = { };
            for (int i = 0; i < 4; ++i)
            {
                if (i > 0)
                {
                    if (position >= text.size() || text[position] != QLatin1Char(','))
                    {
                        return std::nullopt;
                    }
                    ++position;
                }
                if (!readNumber(text, &position, &values[i]))
                {
                    return std::nullopt;
                }
            }
            adjustment.crop = QMargins(values[0], values[1], values[2], values[3]);
        }

        if (position != text.size())
        {
            return std::nullopt;
        }
        return std::make_pair(input, adjustment);
    }

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
};

// ------------------------------------------------- PDFDocumentToolImagesToPdf

std::optional<std::map<int, PDFDocumentToolImagesToPdf::Adjustment>> PDFDocumentToolImagesToPdf::parseAdjustments(const QString& text, int inputCount, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (text.size() > MAXIMUM_ADJUSTMENTS_LENGTH)
    {
        error = tr("The adjustments of the images are longer than %1 characters.").arg(MAXIMUM_ADJUSTMENTS_LENGTH);
        return std::nullopt;
    }

    std::map<int, Adjustment> adjustments;
    for (const QStringView part : QStringView(text).split(QLatin1Char(';')))
    {
        const QStringView item = part.trimmed();
        if (item.isEmpty())
        {
            continue;
        }

        const auto parsed = PDFDocumentToolImagesHelper::parseAdjustment(item);
        if (!parsed)
        {
            error = tr("“%1” is not an adjustment of an image. Write the image and its turn or crop, such as A@90 or B#120,120,120,120.")
                        .arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(item.toString()));
            return std::nullopt;
        }
        if (parsed->first >= inputCount)
        {
            error = tr("The adjustment “%1” names an image, which is not in the list.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(item.toString()));
            return std::nullopt;
        }
        if (!adjustments.emplace(parsed->first, parsed->second).second)
        {
            error = tr("The image %1 has more than one adjustment.").arg(PDFDocumentPagePlan::getInputLetters(parsed->first));
            return std::nullopt;
        }
    }
    return adjustments;
}

QString PDFDocumentToolImagesToPdf::createAdjustments(const std::map<int, Adjustment>& adjustments)
{
    QStringList parts;
    for (const auto& [input, adjustment] : adjustments)
    {
        if (adjustment.isEmpty() || input < 0)
        {
            continue;
        }

        QString text = PDFDocumentPagePlan::getInputLetters(input);
        if (adjustment.turn != PageRotation::None)
        {
            text += QLatin1Char('@') + QString::number(PDFDocumentToolImagesHelper::getAngle(adjustment.turn));
        }
        if (!adjustment.crop.isNull())
        {
            const QMargins& crop = adjustment.crop;
            text += QStringLiteral("#%1,%2,%3,%4").arg(qMax(crop.left(), 0)).arg(qMax(crop.top(), 0)).arg(qMax(crop.right(), 0)).arg(qMax(crop.bottom(), 0));
        }
        parts << text;
    }
    return parts.join(QLatin1Char(';'));
}

QRectF PDFDocumentToolImagesToPdf::getClip(const QMargins& crop, QSize shownSize)
{
    if (crop.isNull() || shownSize.isEmpty())
    {
        return QRectF();
    }

    // At least one pixel of the image stays (336:3159, Apply to: All images on a smaller image)
    const int width = shownSize.width();
    const int height = shownSize.height();
    const int left = qBound(0, crop.left(), width - 1);
    const int right = qBound(0, crop.right(), width - 1 - left);
    const int top = qBound(0, crop.top(), height - 1);
    const int bottom = qBound(0, crop.bottom(), height - 1 - top);
    return QRectF(double(left) / width, double(top) / height, double(width - left - right) / width, double(height - top - bottom) / height);
}

Qt::Alignment PDFDocumentToolImagesToPdf::getAnchor(const QString& anchorId)
{
    // K14: the anchors of all tools are the nine cells of one table (PDFDocumentToolGeometry)
    return PDFDocumentToolGeometry::getAnchor(anchorId);
}

QString PDFDocumentToolImagesToPdf::getAnchorId(Qt::Alignment anchor)
{
    return PDFDocumentToolGeometry::getAnchorName(anchor);
}

std::optional<PDFImagePageLayout> PDFDocumentToolImagesToPdf::createLayout(const QVariantMap& parameters, QString* errorMessage, QString* errorParameter)
{
    using Helper = PDFDocumentToolImagesHelper;

    QString dummy;
    QString dummyParameter;
    QString& error = errorMessage ? *errorMessage : dummy;
    QString& parameter = errorParameter ? *errorParameter : dummyParameter;
    error.clear();
    parameter.clear();

    PDFImagePageLayout layout;
    const QString sizeId = parameters.value(Helper::name(PARAMETER_PAGE_SIZE)).toString();
    if (sizeId == QLatin1String(SIZE_CUSTOM))
    {
        const double width = PDFPageSizePresets::getPoints(parameters.value(Helper::name(PARAMETER_WIDTH)).toDouble());
        const double height = PDFPageSizePresets::getPoints(parameters.value(Helper::name(PARAMETER_HEIGHT)).toDouble());
        if (!PDFPageSizePresets::isValidSide(width) || !PDFPageSizePresets::isValidSide(height))
        {
            error = tr("The width and the height of a page must be between %1 and %2 mm.")
                        .arg(PDFDocumentToolNewDocument::formatMillimeters(PDFPageSizePresets::MINIMUM_SIDE_MILLIMETERS),
                             PDFDocumentToolNewDocument::formatMillimeters(PDFPageSizePresets::MAXIMUM_SIDE_MILLIMETERS));
            parameter = Helper::name(PARAMETER_WIDTH);
            return std::nullopt;
        }
        layout.pageSize = QSizeF(width, height);
    }
    else if (sizeId != QLatin1String(SIZE_FIT_TO_IMAGE))
    {
        const std::optional<QSizeF> size = PDFPageSizePresets::getSize(sizeId);
        if (!size || sizeId == QLatin1String(PDFPageSizePresets::SIZE_16X9))
        {
            error = tr("Unknown page size.");
            parameter = Helper::name(PARAMETER_PAGE_SIZE);
            return std::nullopt;
        }
        layout.pageSize = *size;
    }

    const QString orientation = parameters.value(Helper::name(PARAMETER_ORIENTATION)).toString();
    if (orientation == QLatin1String(ORIENTATION_PORTRAIT))
    {
        layout.orientation = PDFImagePageLayout::Orientation::Portrait;
    }
    else if (orientation == QLatin1String(ORIENTATION_LANDSCAPE))
    {
        layout.orientation = PDFImagePageLayout::Orientation::Landscape;
    }
    else
    {
        layout.orientation = PDFImagePageLayout::Orientation::Auto;
    }

    // The margins: a named or custom page keeps an area (less than half of its shorter side, 5.8)
    const double marginsMillimeters = parameters.value(Helper::name(PARAMETER_MARGINS), DEFAULT_MARGINS_MILLIMETERS).toDouble();
    if (!std::isfinite(marginsMillimeters) || marginsMillimeters < 0.0 || marginsMillimeters > MAXIMUM_MARGINS_MILLIMETERS)
    {
        error = tr("The margins must be between 0 and %1 mm.").arg(PDFDocumentToolNewDocument::formatMillimeters(MAXIMUM_MARGINS_MILLIMETERS));
        parameter = Helper::name(PARAMETER_MARGINS);
        return std::nullopt;
    }
    const double margins = PDFPageSizePresets::getPoints(marginsMillimeters);
    if (!layout.pageSize.isEmpty())
    {
        const double shorterSide = qMin(layout.pageSize.width(), layout.pageSize.height());
        if (2.0 * margins >= shorterSide)
        {
            error = tr("The margins must be less than %1 mm, half of the shorter side of the page.").arg(PDFDocumentToolNewDocument::formatMillimeters(PDFPageSizePresets::getMillimeters(shorterSide * 0.5)));
            parameter = Helper::name(PARAMETER_MARGINS);
            return std::nullopt;
        }
    }
    layout.margins = QMarginsF(margins, margins, margins, margins);

    const QString scaling = parameters.value(Helper::name(PARAMETER_IMAGE_SCALING)).toString();
    if (scaling == QLatin1String(SCALING_FILL))
    {
        layout.scaling = PDFImagePageLayout::Scaling::Fill;
    }
    else if (scaling == QLatin1String(SCALING_STRETCH))
    {
        layout.scaling = PDFImagePageLayout::Scaling::Stretch;
    }
    else if (scaling == QLatin1String(SCALING_ORIGINAL_SIZE))
    {
        layout.scaling = PDFImagePageLayout::Scaling::OriginalSize;
    }
    else
    {
        layout.scaling = PDFImagePageLayout::Scaling::Fit;
    }

    layout.anchor = getAnchor(parameters.value(Helper::name(PARAMETER_ANCHOR)).toString());
    return layout;
}

std::optional<std::vector<PDFDocumentToolImagesToPdf::Input>> PDFDocumentToolImagesToPdf::resolveInputs(const PDFDocumentToolContext& context, const QVariantMap& parameters, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (context.images.size() > size_t(MAXIMUM_INPUT_COUNT))
    {
        error = tr("Images to PDF takes at most %Ln images.", nullptr, MAXIMUM_INPUT_COUNT);
        return std::nullopt;
    }

    auto createInput = [&context](size_t index)
    {
        const PDFDocumentToolInputImage& image = context.images[index];
        Input input;
        input.index = index;
        input.inputId = image.inputId;
        input.name = QFileInfo(image.fileName).fileName();
        input.info = PDFImagePageFactory::readInfo(image.data, image.format);
        return input;
    };

    std::vector<Input> inputs;
    const QString order = parameters.value(PDFDocumentToolImagesHelper::name(PARAMETER_INPUTS)).toString().trimmed();
    if (order.isEmpty())
    {
        for (size_t i = 0; i < context.images.size(); ++i)
        {
            inputs.push_back(createInput(i));
        }
        return inputs;
    }

    std::set<QString> usedIds;
    std::vector<bool> used(context.images.size(), false);
    for (const QString& part : order.split(QLatin1Char(',')))
    {
        const QString id = part.trimmed();
        if (id.isEmpty())
        {
            continue;
        }
        if (!usedIds.insert(id).second)
        {
            error = tr("The image “%1” is in the order of the images more than once.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(id));
            return std::nullopt;
        }

        bool isFound = false;
        for (size_t i = 0; i < context.images.size() && !isFound; ++i)
        {
            if (context.images[i].inputId == id)
            {
                used[i] = true;
                inputs.push_back(createInput(i));
                isFound = true;
            }
        }
        if (!isFound)
        {
            error = tr("The image “%1” is not among the images of the tool.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(id));
            return std::nullopt;
        }
    }

    // Every image has its place: nothing is added or left out silently
    if (std::find(used.cbegin(), used.cend(), false) != used.cend())
    {
        error = tr("An image is missing in the order of the images.");
        return std::nullopt;
    }
    return inputs;
}

QString PDFDocumentToolImagesToPdf::getSuggestedFileName(const QString& firstInputName)
{
    const QString baseName = QFileInfo(firstInputName).completeBaseName();
    return baseName.isEmpty() ? QStringLiteral("Images.pdf") : baseName + QStringLiteral(".pdf");
}

QString PDFDocumentToolImagesToPdf::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolImagesToPdf::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolImagesHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_INPUTS), QT_TR_NOOP("Images"),
                                                   QT_TR_NOOP("Ids of the images in the order of the pages, separated by commas"),
                                                   QString(), MAXIMUM_INPUTS_LENGTH).setStored(false).setPanelOnly(true));
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_ADJUSTMENTS), QT_TR_NOOP("Turn and crop"),
                                                   QT_TR_NOOP("The turn and the crop of the images, such as \"A@90;B#120,120,120,120\""),
                                                   QString(), MAXIMUM_ADJUSTMENTS_LENGTH).setStored(false).setPanelOnly(true));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_PAGE_SIZE), QT_TR_NOOP("Size"), QT_TR_NOOP("Size of the pages"),
                                                 {
                                                     { QString::fromLatin1(SIZE_FIT_TO_IMAGE), QT_TR_NOOP("Fit to image"), QT_TR_NOOP("Each page is exactly as large as its image") },
                                                     { QString::fromLatin1(SIZE_A4), QT_TR_NOOP("A4"), QT_TR_NOOP("210 × 297 mm") },
                                                     { QString::fromLatin1(SIZE_A3), QT_TR_NOOP("A3"), QT_TR_NOOP("297 × 420 mm") },
                                                     { QString::fromLatin1(SIZE_A5), QT_TR_NOOP("A5"), QT_TR_NOOP("148 × 210 mm") },
                                                     { QString::fromLatin1(SIZE_LETTER), QT_TR_NOOP("Letter"), QT_TR_NOOP("216 × 279 mm") },
                                                     { QString::fromLatin1(SIZE_LEGAL), QT_TR_NOOP("Legal"), QT_TR_NOOP("216 × 356 mm") },
                                                     { QString::fromLatin1(SIZE_CUSTOM), QT_TR_NOOP("Custom"), QT_TR_NOOP("Width and height fields appear in the Page layout panel") },
                                                 },
                                                 QString::fromLatin1(SIZE_A4)));
    definitions.push_back(Definition::createReal(tr, Helper::name(PARAMETER_WIDTH), QT_TR_NOOP("Width"), QT_TR_NOOP("Width of a page of the custom size, in millimeters"),
                                                 DEFAULT_WIDTH_MILLIMETERS, PDFPageSizePresets::MINIMUM_SIDE_MILLIMETERS, PDFPageSizePresets::MAXIMUM_SIDE_MILLIMETERS));
    definitions.push_back(Definition::createReal(tr, Helper::name(PARAMETER_HEIGHT), QT_TR_NOOP("Height"), QT_TR_NOOP("Height of a page of the custom size, in millimeters"),
                                                 DEFAULT_HEIGHT_MILLIMETERS, PDFPageSizePresets::MINIMUM_SIDE_MILLIMETERS, PDFPageSizePresets::MAXIMUM_SIDE_MILLIMETERS));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_ORIENTATION), QT_TR_NOOP("Orientation"), QT_TR_NOOP("Orientation of the pages"),
                                                 {
                                                     { QString::fromLatin1(ORIENTATION_AUTO), QT_TR_NOOP("Auto"), QT_TR_NOOP("Each page turns to match its image") },
                                                     { QString::fromLatin1(ORIENTATION_PORTRAIT), QT_TR_NOOP("Portrait"), QT_TR_NOOP("Higher than wide") },
                                                     { QString::fromLatin1(ORIENTATION_LANDSCAPE), QT_TR_NOOP("Landscape"), QT_TR_NOOP("Wider than high") },
                                                 },
                                                 QString::fromLatin1(ORIENTATION_AUTO)));
    definitions.push_back(Definition::createReal(tr, Helper::name(PARAMETER_MARGINS), QT_TR_NOOP("Margins"), QT_TR_NOOP("Margins on every side of the page, in millimeters"),
                                                 DEFAULT_MARGINS_MILLIMETERS, 0.0, MAXIMUM_MARGINS_MILLIMETERS));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_IMAGE_SCALING), QT_TR_NOOP("Image"), QT_TR_NOOP("How the image fills the page inside the margins"),
                                                 {
                                                     { QString::fromLatin1(SCALING_FIT), QT_TR_NOOP("Fit, keep ratio"), QT_TR_NOOP("The whole image is visible inside the margins") },
                                                     { QString::fromLatin1(SCALING_FILL), QT_TR_NOOP("Fill, keep ratio"), QT_TR_NOOP("The page is covered, the edges may be cut") },
                                                     { QString::fromLatin1(SCALING_STRETCH), QT_TR_NOOP("Stretch"), QT_TR_NOOP("Fills the area exactly, proportions may change") },
                                                     { QString::fromLatin1(SCALING_ORIGINAL_SIZE), QT_TR_NOOP("Original size"), QT_TR_NOOP("Uses the resolution stored in the image") },
                                                 },
                                                 QString::fromLatin1(SCALING_FIT)));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_ANCHOR), QT_TR_NOOP("Anchor"), QT_TR_NOOP("Where the image lies inside the margins"),
                                                 {
                                                     { QString::fromLatin1(ANCHOR_TOP_LEFT), QT_TR_NOOP("Top left"), nullptr },
                                                     { QString::fromLatin1(ANCHOR_TOP_CENTER), QT_TR_NOOP("Top center"), nullptr },
                                                     { QString::fromLatin1(ANCHOR_TOP_RIGHT), QT_TR_NOOP("Top right"), nullptr },
                                                     { QString::fromLatin1(ANCHOR_MIDDLE_LEFT), QT_TR_NOOP("Middle left"), nullptr },
                                                     { QString::fromLatin1(ANCHOR_MIDDLE_CENTER), QT_TR_NOOP("Middle center"), nullptr },
                                                     { QString::fromLatin1(ANCHOR_MIDDLE_RIGHT), QT_TR_NOOP("Middle right"), nullptr },
                                                     { QString::fromLatin1(ANCHOR_BOTTOM_LEFT), QT_TR_NOOP("Bottom left"), nullptr },
                                                     { QString::fromLatin1(ANCHOR_BOTTOM_CENTER), QT_TR_NOOP("Bottom center"), nullptr },
                                                     { QString::fromLatin1(ANCHOR_BOTTOM_RIGHT), QT_TR_NOOP("Bottom right"), nullptr },
                                                 },
                                                 QString::fromLatin1(ANCHOR_MIDDLE_CENTER)));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_OPTIMIZE_IMAGES), QT_TR_NOOP("Optimize"), QT_TR_NOOP("Images of the result"),
                                                 {
                                                     { QString::fromLatin1(PDFDocumentToolMerge::OPTIMIZE_OFF), QT_TR_NOOP("Off"), QT_TR_NOOP("Images are stored exactly as they are in the files") },
                                                     { QString::fromLatin1(PDFDocumentToolMerge::OPTIMIZE_PREFER_QUALITY), QT_TR_NOOP("Prefer quality"), QT_TR_NOOP("Downsamples large photos, keeps more detail") },
                                                     { QString::fromLatin1(PDFDocumentToolMerge::OPTIMIZE_MINIMUM_SIZE), QT_TR_NOOP("Minimum size"), QT_TR_NOOP("Smallest file, stronger compression") },
                                                 },
                                                 QString::fromLatin1(PDFDocumentToolMerge::OPTIMIZE_OFF)));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolImagesToPdf::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolImagesHelper;

    // The order and the adjustments belong to the panel (an assistant gives imagePaths in the
    // order of the pages); width and height belong to the custom size (as Blank PDF)
    std::vector<PDFDocumentToolParameterError> errors;
    for (const char* parameter : { PARAMETER_INPUTS, PARAMETER_ADJUSTMENTS })
    {
        if (given.contains(Helper::name(parameter)))
        {
            errors.push_back({ Helper::name(parameter), tr("%1 is not a parameter of an assistant: give the images by imagePaths, in the order of the pages.").arg(Helper::name(parameter)) });
        }
    }

    const bool isCustom = given.value(Helper::name(PARAMETER_PAGE_SIZE)).toString() == QLatin1String(SIZE_CUSTOM);
    for (const char* parameter : { PARAMETER_WIDTH, PARAMETER_HEIGHT })
    {
        if (isCustom && !given.contains(Helper::name(parameter)))
        {
            errors.push_back({ Helper::name(parameter), tr("pageSize custom needs width and height in millimeters.") });
            break;
        }
        if (!isCustom && given.contains(Helper::name(parameter)))
        {
            errors.push_back({ Helper::name(parameter), tr("%1 is used only with pageSize custom. Set pageSize to custom, or leave %1 out.").arg(Helper::name(parameter)) });
        }
    }
    return errors;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolImagesToPdf::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolImagesHelper;

    std::vector<PDFDocumentToolParameterError> errors;
    if (!context.documents.empty())
    {
        errors.push_back({ QString(), tr("Images to PDF takes images, not documents.") });
        return errors;
    }
    if (context.images.empty())
    {
        errors.push_back({ QString(), tr("Add at least one image to create a PDF.") });
        return errors;
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
    for (const Input& input : *inputs)
    {
        if (input.info.frameCount <= 0)
        {
            errors.push_back({ QString(), tr("%1 cannot be read: %2").arg(input.name, input.info.error) });
            return errors;
        }
    }

    if (!parseAdjustments(parameters.value(Helper::name(PARAMETER_ADJUSTMENTS)).toString(), int(inputs->size()), &error))
    {
        errors.push_back({ Helper::name(PARAMETER_ADJUSTMENTS), error });
        return errors;
    }

    QString errorParameter;
    if (!createLayout(parameters, &error, &errorParameter))
    {
        errors.push_back({ errorParameter, error });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolImagesToPdf::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolImagesHelper;

    QString error;
    const std::optional<std::vector<Input>> inputs = resolveInputs(context, parameters, &error);
    if (!inputs)
    {
        return PDFDocumentToolResult::createFailed(error);
    }
    if (inputs->empty())
    {
        return PDFDocumentToolResult::createFailed(tr("Add at least one image to create a PDF."));
    }
    const std::optional<std::map<int, Adjustment>> adjustments = parseAdjustments(parameters.value(Helper::name(PARAMETER_ADJUSTMENTS)).toString(), int(inputs->size()), &error);
    if (!adjustments)
    {
        return PDFDocumentToolResult::createFailed(error);
    }
    const std::optional<PDFImagePageLayout> layout = createLayout(parameters, &error, nullptr);
    if (!layout)
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    // The plan: a page per frame of each image, laid out by the layout with the turn and the
    // crop of the image (the merger decodes each image once, PDFImagePageFactory::readFrames)
    std::vector<PDFDocumentMerger::Source> sources;
    std::vector<PDFDocumentPagePlan::Item> plan;
    for (size_t i = 0; i < inputs->size(); ++i)
    {
        const Input& input = (*inputs)[i];
        const PDFDocumentToolInputImage& image = context.images[input.index];
        sources.push_back(PDFDocumentMerger::Source::createImage(image.data, image.format, image.dotsPerInch, input.name));

        PDFImagePageLayout imageLayout = *layout;
        auto adjustment = adjustments->find(int(i));
        if (adjustment != adjustments->cend())
        {
            imageLayout.turn = adjustment->second.turn;
            imageLayout.clip = getClip(adjustment->second.crop, input.info.size);
        }

        for (int frame = 0; frame < qMax(input.info.frameCount, 1); ++frame)
        {
            PDFDocumentPagePlan::Item item;
            item.input = int(i);
            item.pageIndex = frame;
            item.imageLayout = imageLayout;
            plan.push_back(std::move(item));
        }
    }

    const std::optional<PDFImageOptimizer::Settings> imageSettings = PDFDocumentToolMerge::createImageSettings(parameters);

    PDFProgressPhases phases(progress);
    const int pagesPhase = phases.addPhase(70, tr("Creating the pages"), true);
    const int imagesPhase = imageSettings ? phases.addPhase(20, tr("Optimizing the images"), true) : -1;
    const int measurePhase = phases.addPhase(10, tr("Measuring the result"), false);
    phases.start();

    phases.beginPhase(pagesPhase);
    PDFDocumentMerger::Settings settings;
    settings.outline = PDFDocumentMerger::OutlineMode::NoOutline;
    PDFDocumentMerger::Result merged = PDFDocumentMerger::merge(sources, PDFDocumentPagePlan::createMergerItems(plan), settings, control, phases.getPhase(pagesPhase));
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
    QByteArray writtenData = PDFDocumentToolNewDocument::write(&output, &error);
    if (writtenData.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(error);
    }
    phases.finish();

    PDFDocumentToolResult result;
    const qint64 pageCount = merged.statistics.pageCount;
    const qint64 imageCount = qint64(inputs->size()) - qint64(merged.leftOut.size());
    // Both numbers are counts, each one has its own %Ln
    const QString images = tr("%Ln images", nullptr, int(qMin<qint64>(imageCount, std::numeric_limits<int>::max())));
    result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), tr("%Ln pages from %1", nullptr, int(pageCount)).arg(images) });
    result.facts.push_back({ QStringLiteral("size"), tr("Size"), PDFDocumentToolFormat::formatSize(writtenData.size()) });
    if (imageSettings)
    {
        result.facts.push_back({ QStringLiteral("images"), tr("Images"), tr("%Ln recompressed", nullptr, int(imagesRecompressed)) });
    }

    // An image, which could not be read during the run (owner answer Q5 of step 2)
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
            names << leftOut.name;
        }
        result.facts.push_back({ QStringLiteral("leftOut"), tr("Skipped"), names.join(QStringLiteral(", ")) });
    }

    const QString sizeId = parameters.value(Helper::name(PARAMETER_PAGE_SIZE)).toString();
    result.data.insert(QStringLiteral("pageCount"), pageCount);
    result.data.insert(QStringLiteral("imageCount"), imageCount);
    result.data.insert(QStringLiteral("pageSize"), sizeId);
    result.data.insert(QStringLiteral("orientation"), parameters.value(Helper::name(PARAMETER_ORIENTATION)).toString());
    result.data.insert(QStringLiteral("imageScaling"), parameters.value(Helper::name(PARAMETER_IMAGE_SCALING)).toString());
    result.data.insert(QStringLiteral("anchor"), getAnchorId(layout->anchor));
    result.data.insert(QStringLiteral("marginsMillimeters"), parameters.value(Helper::name(PARAMETER_MARGINS), DEFAULT_MARGINS_MILLIMETERS).toDouble());
    result.data.insert(QStringLiteral("optimizeImages"), parameters.value(Helper::name(PARAMETER_OPTIMIZE_IMAGES)).toString());
    result.data.insert(QStringLiteral("imagesRecompressed"), imagesRecompressed);
    result.data.insert(QStringLiteral("leftOutCount"), qint64(result.leftOutInputs.size()));
    result.data.insert(QStringLiteral("sizeAfter"), qint64(writtenData.size()));

    PDFDocumentToolNewDocument::addToResult(&result, std::move(output), std::move(writtenData), getSuggestedFileName(inputs->front().name));
    return result;
}

// -------------------------------------------------- PDFDocumentToolInterleave

PDFDocumentPagePlan::InterleaveSettings PDFDocumentToolInterleave::createSettings(const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolImagesHelper;

    PDFDocumentPagePlan::InterleaveSettings settings;
    settings.isBackReversed = parameters.value(Helper::name(PARAMETER_BACK_SIDES)).toString() != QLatin1String(BACK_SIDES_IN_ORDER);
    settings.areExtrasKept = parameters.value(Helper::name(PARAMETER_EXTRA_PAGES)).toString() != QLatin1String(EXTRA_PAGES_LEAVE_OUT);
    return settings;
}

QString PDFDocumentToolInterleave::getSuggestedFileName(const QString& frontName)
{
    const QString baseName = QFileInfo(frontName).completeBaseName();
    return baseName.isEmpty() ? QStringLiteral("interleaved.pdf") : baseName + QStringLiteral("_interleaved.pdf");
}

QString PDFDocumentToolInterleave::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolInterleave::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolImagesHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_INPUTS), QT_TR_NOOP("Files"),
                                                   QT_TR_NOOP("Ids of the front sides and of the back sides, separated by a comma"),
                                                   QString(), PDFDocumentToolMerge::MAXIMUM_INPUTS_LENGTH).setStored(false).setPanelOnly(true));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_BACK_SIDES), QT_TR_NOOP("Back sides"), QT_TR_NOOP("Order of the pages of the back sides"),
                                                 {
                                                     { QString::fromLatin1(BACK_SIDES_REVERSED), QT_TR_NOOP("Reversed"), QT_TR_NOOP("The flipped stack was scanned from the last sheet – the usual case") },
                                                     { QString::fromLatin1(BACK_SIDES_IN_ORDER), QT_TR_NOOP("In order"), QT_TR_NOOP("The back sides are in the same order as the front sides") },
                                                 },
                                                 QString::fromLatin1(BACK_SIDES_REVERSED)));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_EXTRA_PAGES), QT_TR_NOOP("Extra pages"), QT_TR_NOOP("Pages of the longer file without a pair"),
                                                 {
                                                     { QString::fromLatin1(EXTRA_PAGES_AT_THE_END), QT_TR_NOOP("At the end"), QT_TR_NOOP("Pages without a partner follow the mixed pages") },
                                                     { QString::fromLatin1(EXTRA_PAGES_LEAVE_OUT), QT_TR_NOOP("Leave out"), QT_TR_NOOP("Pages without a partner are not used") },
                                                 },
                                                 QString::fromLatin1(EXTRA_PAGES_AT_THE_END)));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolInterleave::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolImagesHelper;

    std::vector<PDFDocumentToolParameterError> errors;
    if (given.contains(Helper::name(PARAMETER_INPUTS)))
    {
        errors.push_back({ Helper::name(PARAMETER_INPUTS), tr("inputs is not a parameter of an assistant: give the front sides and the back sides by documentIds, in this order.") });
    }
    return errors;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolInterleave::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    // Exactly two PDF documents (K29: an image is not a scan of Interleave); the order, the
    // inputs and the permissions are those of Merge (Q4: owner answer Q2 of step 2)
    std::vector<PDFDocumentToolParameterError> errors;
    if (!context.images.empty())
    {
        errors.push_back({ QString(), tr("Interleave needs PDF files.") });
        return errors;
    }
    if (context.documents.size() != 2)
    {
        errors.push_back({ QString(), tr("Interleave needs two files: the front sides and the back sides.") });
        return errors;
    }

    QVariantMap mergeParameters;
    mergeParameters.insert(QString::fromLatin1(PDFDocumentToolMerge::PARAMETER_INPUTS), parameters.value(QString::fromLatin1(PARAMETER_INPUTS)));
    return PDFDocumentToolMerge().validate(context, mergeParameters);
}

PDFDocumentToolResult PDFDocumentToolInterleave::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolImagesHelper;

    if (context.documents.size() != 2 || !context.images.empty())
    {
        return PDFDocumentToolResult::createFailed(tr("Interleave needs two files: the front sides and the back sides."));
    }

    QVariantMap mergeParameters;
    mergeParameters.insert(QString::fromLatin1(PDFDocumentToolMerge::PARAMETER_INPUTS), parameters.value(Helper::name(PARAMETER_INPUTS)));
    QString error;
    const std::optional<std::vector<PDFDocumentToolMerge::Input>> inputs = PDFDocumentToolMerge::resolveInputs(context, mergeParameters, &error);
    if (!inputs || inputs->size() != 2)
    {
        return PDFDocumentToolResult::createFailed(error.isEmpty() ? tr("Interleave needs two files: the front sides and the back sides.") : error);
    }

    // The plan of the sides (5.7) through the merge of Merge: no outline and no document parts (Q8)
    const PDFInteger frontCount = (*inputs)[0].pageCount;
    const PDFInteger backCount = (*inputs)[1].pageCount;
    const PDFDocumentPagePlan::InterleaveSettings settings = createSettings(parameters);
    const std::vector<PDFDocumentPagePlan::Item> plan = PDFDocumentPagePlan::interleave(frontCount, backCount, settings);
    if (plan.empty())
    {
        return PDFDocumentToolResult::createFailed(tr("The result has no page."));
    }
    mergeParameters.insert(QString::fromLatin1(PDFDocumentToolMerge::PARAMETER_PLAN), PDFDocumentPagePlan::toText(plan));
    mergeParameters.insert(QString::fromLatin1(PDFDocumentToolMerge::PARAMETER_OUTLINE), QString::fromLatin1(PDFDocumentToolMerge::OUTLINE_NONE));
    mergeParameters.insert(QString::fromLatin1(PDFDocumentToolMerge::PARAMETER_OPTIMIZE_IMAGES), QString::fromLatin1(PDFDocumentToolMerge::OPTIMIZE_OFF));

    PDFDocumentToolResult merged = PDFDocumentToolMerge().execute(context, mergeParameters, control, progress);
    if (!merged.isSuccess())
    {
        return merged;
    }

    // The facts of Interleave (385:14973) in the place of those of Merge; the size and the steps stay
    PDFDocumentToolResult result = std::move(merged);
    QString sizeText;
    std::vector<PDFDocumentToolFact> keptFacts;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        if (fact.id == QLatin1String("size"))
        {
            sizeText = fact.value;
        }
        else if (fact.id != QLatin1String("pages") && fact.id != QLatin1String("outline"))
        {
            // Renamed fields, removed signatures and tags, a left out input: as Merge
            keptFacts.push_back(fact);
        }
    }

    const PDFInteger pairs = qMin(frontCount, backCount);
    const PDFInteger unpaired = qMax(frontCount, backCount) - pairs;
    const PDFInteger frontInResult = settings.areExtrasKept ? frontCount : pairs;
    const PDFInteger backInResult = settings.areExtrasKept ? backCount : pairs;
    const PDFInteger pageCount = PDFInteger(plan.size());

    result.facts.clear();
    result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), tr("%1 · %2 front and %3 back sides").arg(pageCount).arg(frontInResult).arg(backInResult) });
    result.facts.push_back({ QStringLiteral("backSides"), tr("Back sides"), settings.isBackReversed ? tr("Reversed order") : tr("In order") });
    if (unpaired > 0)
    {
        const bool isFrontLonger = frontCount > backCount;
        QString text;
        if (settings.areExtrasKept)
        {
            text = isFrontLonger ? tr("%Ln front sides without a back side at the end", nullptr, int(unpaired)) : tr("%Ln back sides without a front side at the end", nullptr, int(unpaired));
        }
        else
        {
            text = isFrontLonger ? tr("%Ln front sides without a back side left out", nullptr, int(unpaired)) : tr("%Ln back sides without a front side left out", nullptr, int(unpaired));
        }
        result.facts.push_back({ QStringLiteral("extraPages"), tr("Extra pages"), text });
    }

    // Q8: the bookmarks of the scans are not kept
    bool hasBookmarks = false;
    for (const PDFDocumentToolInputDocument& document : context.documents)
    {
        const QSharedPointer<PDFOutlineItem> outline = document.document ? document.document->getCatalog()->getOutlineRootPtr() : QSharedPointer<PDFOutlineItem>();
        hasBookmarks = hasBookmarks || (outline && outline->getChildCount() > 0);
    }
    if (hasBookmarks)
    {
        result.facts.push_back({ QStringLiteral("bookmarks"), tr("Bookmarks"), tr("Bookmarks of the scans are not kept") });
    }
    result.facts.insert(result.facts.end(), keptFacts.begin(), keptFacts.end());
    if (!sizeText.isEmpty())
    {
        result.facts.push_back({ QStringLiteral("size"), tr("Size"), sizeText });
    }

    result.data.insert(QStringLiteral("frontPageCount"), qint64(frontCount));
    result.data.insert(QStringLiteral("backPageCount"), qint64(backCount));
    result.data.insert(QStringLiteral("unpairedCount"), qint64(unpaired));
    result.data.insert(QStringLiteral("backSides"), QString::fromLatin1(settings.isBackReversed ? BACK_SIDES_REVERSED : BACK_SIDES_IN_ORDER));
    result.data.insert(QStringLiteral("extraPages"), QString::fromLatin1(settings.areExtrasKept ? EXTRA_PAGES_AT_THE_END : EXTRA_PAGES_LEAVE_OUT));
    result.data.insert(QStringLiteral("hasBookmarks"), hasBookmarks);
    result.data.remove(QStringLiteral("outline"));
    result.data.remove(QStringLiteral("optimizeImages"));
    result.data.remove(QStringLiteral("imagesRecompressed"));
    for (PDFDocumentToolOutputDocument& output : result.documents)
    {
        output.suggestedFileName = getSuggestedFileName((*inputs)[0].name);
    }
    return result;
}

}   // namespace pdf
