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

#include "pdfdocumenttool_geometry.h"

#include "pdfcatalog.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumenttool_merge.h"
#include "pdfexception.h"
#include "pdfform.h"
#include "pdfoperationcontrol.h"
#include "pdfpagesizepresets.h"
#include "pdfsecurityhandler.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace pdf
{

/// Helpers of the tools of the page geometry
class PDFDocumentToolGeometryHelper
{
public:
    PDFDocumentToolGeometryHelper() = delete;

    /// A value written by the user may differ from the frame by the rounding of a conversion
    static constexpr double TOLERANCE_MM = 0.01;

    /// Most margins and offsets in millimeters (the largest side of a page)
    static constexpr double MAXIMUM_LENGTH_MM = PDFPageSizePresets::MAXIMUM_SIDE_MILLIMETERS;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    static double readReal(const QVariantMap& parameters, const char* parameter)
    {
        return parameters.value(name(parameter)).toDouble();
    }

    /// Numbers of the pages (from 1) for MCP
    static QJsonArray getPageNumbers(const std::vector<PDFInteger>& pages)
    {
        QJsonArray numbers;
        for (const PDFInteger page : pages)
        {
            numbers.append(qint64(page + 1));
        }
        return numbers;
    }

    /// The changed document of the result with its suggested name; a signed document is saved by
    /// an incremental update. No level of a certification (DocMDP /P, ISO 32000-2 12.8.2.2,
    /// Table 257) allows to change the boxes of pages (Q12, R10): the certification is removed
    /// from the changed document (the values of the signatures, /Perms, /DSS - as Rotate,
    /// Remove and Insert), it is saved only as a copy written whole ("lost-copy-only")
    static void addDocument(PDFDocumentToolResult* result, const PDFDocumentToolInputDocument& input, PDFDocument document, const QString& suffix, PDFModifiedDocument::ModificationFlags flags)
    {
        const bool isCertified = PDFDocumentToolGeometry::isCertified(input.document.data());
        PDFInteger certificationSignaturesRemoved = 0;
        if (isCertified)
        {
            document = PDFDocMDPPermissions::removeCertification(document, &certificationSignaturesRemoved);
        }

        PDFDocumentToolOutputDocument output;
        output.kind = PDFDocumentToolOutputDocument::Kind::Changed;
        output.inputIndex = 0;
        output.document = PDFDocumentPointer(new PDFDocument(std::move(document)));
        output.flags = flags;
        const QString baseName = QFileInfo(input.fileName).completeBaseName();
        output.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + suffix + QStringLiteral(".pdf");
        result->documents.push_back(std::move(output));

        result->data.insert(QStringLiteral("signatureCount"), PDFForm::getSignatureCount(input.document.data()));
        result->data.insert(QStringLiteral("signatureImpact"), QStringLiteral("incremental-save"));
        result->data.insert(QStringLiteral("certified"), isCertified);
        if (isCertified)
        {
            PDFDocMDPPermissions::addCertificationRemoved(result, certificationSignaturesRemoved);
        }
    }

    /// The page selection of the parameter follows the page objects
    static void mapSelection(QVariantMap* parameters, const char* parameter, const PDFDocument* from, const std::vector<PDFInteger>& pageMap)
    {
        const QString key = name(parameter);
        if (!parameters->contains(key))
        {
            return;
        }

        QString error;
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters->value(key), &error);
        if (selection)
        {
            parameters->insert(key, selection->mapPages(from, pageMap).toVariant());
        }
    }

    /// The shown size of a box of a page in millimeters
    static QSizeF getShownSizeMM(const PDFPage* page, const QRectF& box)
    {
        const QSizeF size = PDFPage::getRotatedBox(box, page->getPageRotation()).size();
        const PDFReal factor = page->getMillimetersToUnits();
        return QSizeF(size.width() / factor, size.height() / factor);
    }

    static bool isSame(const QRectF& first, const QRectF& second)
    {
        const QRectF a = first.normalized();
        const QRectF b = second.normalized();
        constexpr PDFReal tolerance = 1e-4;
        return std::abs(a.left() - b.left()) < tolerance && std::abs(a.top() - b.top()) < tolerance &&
               std::abs(a.right() - b.right()) < tolerance && std::abs(a.bottom() - b.bottom()) < tolerance;
    }

    /// A margin parameter in millimeters
    static PDFDocumentToolParameterDefinition createMargin(PDFDocumentToolTranslator translator, const char* parameter, PDFDocumentToolText title, PDFDocumentToolText description, double defaultValue, double minimum)
    {
        return PDFDocumentToolParameterDefinition::createReal(translator, name(parameter), title, description, defaultValue, minimum, MAXIMUM_LENGTH_MM);
    }

    /// The nine anchors (391:23821)
    static std::vector<std::pair<Qt::Alignment, const char*>> getAnchors()
    {
        return {
            { Qt::AlignTop | Qt::AlignLeft, "top-left" },
            { Qt::AlignTop | Qt::AlignHCenter, "top-center" },
            { Qt::AlignTop | Qt::AlignRight, "top-right" },
            { Qt::AlignVCenter | Qt::AlignLeft, "middle-left" },
            { Qt::AlignCenter, "middle-center" },
            { Qt::AlignVCenter | Qt::AlignRight, "middle-right" },
            { Qt::AlignBottom | Qt::AlignLeft, "bottom-left" },
            { Qt::AlignBottom | Qt::AlignHCenter, "bottom-center" },
            { Qt::AlignBottom | Qt::AlignRight, "bottom-right" },
        };
    }
};

// ------------------------------------------------------------- PDFDocumentToolGeometry

QString PDFDocumentToolGeometry::getAnchorName(Qt::Alignment anchor)
{
    // A missing part is the middle
    Qt::Alignment normalized = anchor & (Qt::AlignLeft | Qt::AlignRight | Qt::AlignHCenter | Qt::AlignTop | Qt::AlignBottom | Qt::AlignVCenter);
    if (!(normalized & Qt::AlignHorizontal_Mask))
    {
        normalized |= Qt::AlignHCenter;
    }
    if (!(normalized & Qt::AlignVertical_Mask))
    {
        normalized |= Qt::AlignVCenter;
    }
    for (const auto& [alignment, anchorName] : PDFDocumentToolGeometryHelper::getAnchors())
    {
        if (alignment == normalized)
        {
            return QString::fromLatin1(anchorName);
        }
    }
    return QStringLiteral("middle-center");
}

Qt::Alignment PDFDocumentToolGeometry::getAnchor(const QString& name)
{
    for (const auto& [alignment, anchorName] : PDFDocumentToolGeometryHelper::getAnchors())
    {
        if (name == QLatin1String(anchorName))
        {
            return alignment;
        }
    }
    return Qt::AlignCenter;
}

QString PDFDocumentToolGeometry::formatSizeMM(const QSizeF& sizeMM)
{
    // 389:24633 "190 × 277 mm": whole millimeters
    const QLocale locale;
    return tr("%1 × %2 mm").arg(locale.toString(qRound64(sizeMM.width())), locale.toString(qRound64(sizeMM.height())));
}

std::vector<PDFInteger> PDFDocumentToolGeometry::getPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();
    if (!document)
    {
        error = tr("There is no document.");
        return {};
    }

    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters.value(QStringLiteral("pages")), &error);
    if (!selection)
    {
        return {};
    }
    return selection->getPageIndices(document, &error);
}

bool PDFDocumentToolGeometry::isCertified(const PDFDocument* document)
{
    return !PDFDocMDPPermissions::isPageChangeAllowed(PDFDocMDPPermissions::getLevel(document));
}

// ------------------------------------------------------------ PDFDocumentToolCropPages

PDFPageGeometry::CropSettings PDFDocumentToolCropPages::getSettings(const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolGeometryHelper;

    PDFPageGeometry::CropSettings settings;
    const QString mode = parameters.value(Helper::name(PARAMETER_MODE)).toString();
    if (mode == QLatin1String(MODE_CUSTOM_BOX))
    {
        settings.mode = PDFPageGeometry::CropMode::CustomBox;
    }
    else if (mode == QLatin1String(MODE_REMOVE))
    {
        settings.mode = PDFPageGeometry::CropMode::Remove;
    }
    settings.marginMM = Helper::readReal(parameters, PARAMETER_MARGIN);
    settings.boxMM = QRectF(Helper::readReal(parameters, PARAMETER_LEFT), Helper::readReal(parameters, PARAMETER_TOP),
                            Helper::readReal(parameters, PARAMETER_WIDTH), Helper::readReal(parameters, PARAMETER_HEIGHT));
    return settings;
}

PDFInteger PDFDocumentToolCropPages::getReferencePage(const PDFDocument* document, const QVariantMap& parameters, const std::vector<PDFInteger>& pages)
{
    const PDFInteger pageNumber = parameters.value(PDFDocumentToolGeometryHelper::name(PARAMETER_REFERENCE_PAGE)).toLongLong();
    if (document && pageNumber >= 1 && size_t(pageNumber) <= document->getCatalog()->getPageCount())
    {
        return pageNumber - 1;
    }
    return pages.empty() ? 0 : pages.front();
}

QString PDFDocumentToolCropPages::checkCustomBox(const PDFPage* referencePage, const QRectF& boxMM)
{
    using Helper = PDFDocumentToolGeometryHelper;
    const auto format = PDFDocumentToolNewDocument::formatMillimeters;

    if (!referencePage)
    {
        return tr("There is no page.");
    }
    for (const double value : { boxMM.left(), boxMM.top(), boxMM.width(), boxMM.height() })
    {
        if (!std::isfinite(value))
        {
            return tr("Type the left edge, the top edge, the width and the height of the crop box.");
        }
    }

    const QSizeF page = PDFPageGeometry::getShownSizeMM(referencePage);
    if (boxMM.left() < -Helper::TOLERANCE_MM)
    {
        return tr("The left edge %1 mm is outside the page. The left edge can be 0 mm at least.").arg(format(boxMM.left()));
    }
    if (boxMM.top() < -Helper::TOLERANCE_MM)
    {
        return tr("The top edge %1 mm is outside the page. The top edge can be 0 mm at least.").arg(format(boxMM.top()));
    }
    if (boxMM.left() >= page.width())
    {
        return tr("Left %1 mm is outside the %2 mm wide page.").arg(format(boxMM.left()), format(page.width()));
    }
    if (boxMM.top() >= page.height())
    {
        return tr("Top %1 mm is outside the %2 mm high page.").arg(format(boxMM.top()), format(page.height()));
    }
    if (boxMM.left() + boxMM.width() > page.width() + Helper::TOLERANCE_MM)
    {
        // 398:28158
        return tr("Left %1 mm + width %2 mm ends %3 mm outside the %4 mm wide page. The width can be %5 mm at most.")
                .arg(format(boxMM.left()), format(boxMM.width()), format(boxMM.left() + boxMM.width() - page.width()), format(page.width()), format(page.width() - boxMM.left()));
    }
    if (boxMM.top() + boxMM.height() > page.height() + Helper::TOLERANCE_MM)
    {
        return tr("Top %1 mm + height %2 mm ends %3 mm outside the %4 mm high page. The height can be %5 mm at most.")
                .arg(format(boxMM.top()), format(boxMM.height()), format(boxMM.top() + boxMM.height() - page.height()), format(page.height()), format(page.height() - boxMM.top()));
    }
    return QString();
}

std::optional<std::vector<PDFPageGeometryChange>> PDFDocumentToolCropPages::createChanges(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    const std::vector<PDFInteger> pages = PDFDocumentToolGeometry::getPages(document, parameters, &error);
    if (pages.empty())
    {
        return std::nullopt;
    }

    const PDFCatalog* catalog = document->getCatalog();
    const PDFPageGeometry::CropSettings settings = getSettings(parameters);
    const PDFInteger referenceIndex = getReferencePage(document, parameters, pages);
    const PDFPage* referencePage = catalog->getPage(size_t(referenceIndex));
    if (settings.mode == PDFPageGeometry::CropMode::CustomBox)
    {
        error = checkCustomBox(referencePage, settings.boxMM);
        if (!error.isEmpty())
        {
            return std::nullopt;
        }
    }

    std::vector<PDFPageGeometryChange> changes;
    std::vector<PDFInteger> tooSmallPages;
    changes.reserve(pages.size());
    for (const PDFInteger pageIndex : pages)
    {
        const std::optional<QRectF> cropBox = PDFPageGeometry::computeCropBox(catalog->getPage(size_t(pageIndex)), referencePage, settings);
        if (!cropBox)
        {
            tooSmallPages.push_back(pageIndex);
            continue;
        }

        PDFPageGeometryChange change;
        change.pageIndex = pageIndex;
        change.cropBox = *cropBox;
        changes.push_back(std::move(change));
    }

    if (!tooSmallPages.empty())
    {
        // 5.3: less than 3 points of a page would stay visible (PDF Reference 1.7, Table C.1)
        const QString pageNumbers = PDFDocumentPageOperations::formatPageNumbers(tooSmallPages);
        error = settings.mode == PDFPageGeometry::CropMode::EqualMargins
                    ? tr("A margin of %1 mm leaves nothing of %Ln pages: %2. Choose a smaller margin.", nullptr, int(tooSmallPages.size())).arg(PDFDocumentToolNewDocument::formatMillimeters(settings.marginMM), pageNumbers)
                    : tr("The margins of the crop box leave nothing of %Ln pages: %1. They are smaller than page %2.", nullptr, int(tooSmallPages.size())).arg(pageNumbers).arg(referenceIndex + 1);
        return std::nullopt;
    }
    return changes;
}

QString PDFDocumentToolCropPages::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolCropPages::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolGeometryHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, Helper::name(PARAMETER_PAGES), QT_TR_NOOP("Pages to crop"), QT_TR_NOOP("The pages, whose crop box changes")).setStored(false));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_MODE), QT_TR_NOOP("Crop"), QT_TR_NOOP("How the visible area of the pages is set"),
                                                 {
                                                     { QString::fromLatin1(MODE_EQUAL_MARGINS), QT_TR_NOOP("Equal margins"), QT_TR_NOOP("Cuts the same margin from all four sides"), QStringLiteral("margins") },
                                                     { QString::fromLatin1(MODE_CUSTOM_BOX), QT_TR_NOOP("Custom box"), QT_TR_NOOP("Draw the box on the page or type its position and size"), QStringLiteral("crop-handles") },
                                                     { QString::fromLatin1(MODE_REMOVE), QT_TR_NOOP("Remove Crop"), QT_TR_NOOP("The whole page is shown again"), QStringLiteral("crop-off") },
                                                 },
                                                 QString::fromLatin1(MODE_EQUAL_MARGINS)));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_MARGIN, QT_TR_NOOP("Margin"), QT_TR_NOOP("The same margin is cut from all four sides, in millimeters (equal-margins)"), 10.0, 0.0));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_LEFT, QT_TR_NOOP("Left"), QT_TR_NOOP("Left edge of the crop box from the left side of the reference page as it is shown, in millimeters (custom-box)"), 20.0, 0.0));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_TOP, QT_TR_NOOP("Top"), QT_TR_NOOP("Top edge of the crop box from the top of the reference page as it is shown, in millimeters (custom-box)"), 25.0, 0.0));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_WIDTH, QT_TR_NOOP("Width"), QT_TR_NOOP("Width of the crop box in millimeters (custom-box)"), 170.0, PDFPageSizePresets::MINIMUM_SIDE_MILLIMETERS));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_HEIGHT, QT_TR_NOOP("Height"), QT_TR_NOOP("Height of the crop box in millimeters (custom-box)"), 240.0, PDFPageSizePresets::MINIMUM_SIDE_MILLIMETERS));
    definitions.push_back(Definition::createInteger(tr, Helper::name(PARAMETER_REFERENCE_PAGE), QT_TR_NOOP("Reference page"),
                                                    QT_TR_NOOP("The page of the preview, whose box the custom box is (0 = the first page of the selection); the other pages get the same margins"),
                                                    0, 0, PDFDocumentToolPageSelection::MAXIMUM_PAGE_NUMBER).setStored(false).setPanelOnly(true));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolCropPages::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolGeometryHelper;

    // A value of the other mode would be ignored
    std::vector<PDFDocumentToolParameterError> errors;
    const QString mode = given.value(Helper::name(PARAMETER_MODE), QString::fromLatin1(MODE_EQUAL_MARGINS)).toString();
    if (mode != QLatin1String(MODE_EQUAL_MARGINS) && given.contains(Helper::name(PARAMETER_MARGIN)))
    {
        errors.push_back({ Helper::name(PARAMETER_MARGIN), tr("margin is used only with the mode equal-margins. Leave it out, or set mode to equal-margins.") });
    }
    for (const char* parameter : { PARAMETER_LEFT, PARAMETER_TOP, PARAMETER_WIDTH, PARAMETER_HEIGHT })
    {
        if (mode != QLatin1String(MODE_CUSTOM_BOX) && given.contains(Helper::name(parameter)))
        {
            errors.push_back({ Helper::name(parameter), tr("%1 is used only with the mode custom-box. Leave it out, or set mode to custom-box.").arg(Helper::name(parameter)) });
        }
    }
    return errors;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolCropPages::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    QString error;
    if (!createChanges(context.getDocument(), parameters, &error))
    {
        const PDFPageGeometry::CropSettings settings = getSettings(parameters);
        const char* parameter = settings.mode == PDFPageGeometry::CropMode::CustomBox ? PARAMETER_WIDTH : PARAMETER_MARGIN;
        QString pagesError;
        if (PDFDocumentToolGeometry::getPages(context.getDocument(), parameters, &pagesError).empty())
        {
            parameter = PARAMETER_PAGES;
        }
        errors.push_back({ PDFDocumentToolGeometryHelper::name(parameter), error });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolCropPages::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(progress);
    using Helper = PDFDocumentToolGeometryHelper;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const PDFCatalog* catalog = document->getCatalog();

    QString error;
    std::optional<std::vector<PDFPageGeometryChange>> changes = createChanges(document, parameters, &error);
    if (!changes)
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    // Only a page, whose crop box changes; the crop box is written also when only an ancestor
    // has it (Remove: the inherited crop box must not stay)
    std::vector<PDFPageGeometryChange> effective;
    std::vector<PDFInteger> pages;
    bool hasDefaultBoxes = false;
    for (PDFPageGeometryChange& change : *changes)
    {
        const PDFPage* page = catalog->getPage(size_t(change.pageIndex));
        const bool isDefined = PDFPageGeometry::isBoxDefined(page, &document->getStorage(), PDFPageGeometry::Box::CropBox);
        if (Helper::isSame(page->getCropBox(), *change.cropBox) && (isDefined || Helper::isSame(*change.cropBox, page->getMediaBox())))
        {
            continue;
        }
        for (const PDFPageGeometry::Box box : { PDFPageGeometry::Box::BleedBox, PDFPageGeometry::Box::TrimBox, PDFPageGeometry::Box::ArtBox })
        {
            hasDefaultBoxes = hasDefaultBoxes || !PDFPageGeometry::isBoxDefined(page, &document->getStorage(), box);
        }
        pages.push_back(change.pageIndex);
        effective.push_back(std::move(change));
    }

    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    const PDFPageGeometry::CropSettings settings = getSettings(parameters);
    const bool isRemove = settings.mode == PDFPageGeometry::CropMode::Remove;
    if (effective.empty())
    {
        return PDFDocumentToolResult::createNothingToDo(isRemove ? tr("The pages have no crop.") : tr("The pages have this crop already."));
    }

    PDFPageGeometryStatistics statistics;
    PDFDocument output = PDFPageGeometry::applyChanges(document, effective, control, &statistics);
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    // The sizes of the reference page (the page of the preview) or of the first cropped page
    PDFInteger referenceIndex = getReferencePage(document, parameters, pages);
    if (std::find(pages.cbegin(), pages.cend(), referenceIndex) == pages.cend())
    {
        referenceIndex = pages.front();
    }
    const QSizeF before = PDFPageGeometry::getShownSizeMM(catalog->getPage(size_t(referenceIndex)));
    const QSizeF after = PDFPageGeometry::getShownSizeMM(output.getCatalog()->getPage(size_t(referenceIndex)));

    PDFDocumentToolResult result;
    result.facts.push_back({ QStringLiteral("cropped"), isRemove ? tr("Crop removed") : tr("Cropped"), tr("%Ln pages", nullptr, int(pages.size())) });
    result.facts.push_back({ QStringLiteral("pageSize"), tr("Page size"), tr("%1 (was %2)").arg(PDFDocumentToolGeometry::formatSizeMM(after), PDFDocumentToolGeometry::formatSizeMM(before)) });
    result.facts.push_back({ QStringLiteral("hiddenContent"), tr("Hidden content"), isRemove ? tr("Visible again") : tr("Kept in the file") });
    if (hasDefaultBoxes && !isRemove)
    {
        // 5.1: the bleed, trim and art boxes default to the crop box (Table 31)
        result.facts.push_back({ QStringLiteral("pageBoxes"), tr("Page boxes"), tr("Bleed, trim and art box follow the crop box") });
    }

    result.data.insert(QStringLiteral("croppedCount"), qint64(pages.size()));
    result.data.insert(QStringLiteral("croppedPages"), Helper::getPageNumbers(pages));
    result.data.insert(QStringLiteral("mode"), parameters.value(Helper::name(PARAMETER_MODE)).toString());
    result.data.insert(QStringLiteral("pageSizeBefore"), QJsonArray({ before.width(), before.height() }));
    result.data.insert(QStringLiteral("pageSizeAfter"), QJsonArray({ after.width(), after.height() }));
    result.data.insert(QStringLiteral("thumbnailsRemoved"), statistics.thumbnailsRemoved);
    Helper::addDocument(&result, input, std::move(output), isRemove ? QStringLiteral("_uncropped") : QStringLiteral("_cropped"), PDFModifiedDocument::Reset);
    return result;
}

QVariantMap PDFDocumentToolCropPages::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    using Helper = PDFDocumentToolGeometryHelper;

    if (!from || !to || from == to)
    {
        return parameters;
    }

    const std::vector<PDFInteger> pageMap = PDFDocumentToolPageSelection::mapPageIndices(from, to);
    QVariantMap mapped = parameters;
    Helper::mapSelection(&mapped, PARAMETER_PAGES, from, pageMap);

    // The reference page follows its page object, a page, which is gone, is none
    const PDFInteger reference = parameters.value(Helper::name(PARAMETER_REFERENCE_PAGE)).toLongLong();
    if (reference >= 1)
    {
        const PDFInteger mappedIndex = size_t(reference - 1) < pageMap.size() ? pageMap[size_t(reference - 1)] : -1;
        mapped.insert(Helper::name(PARAMETER_REFERENCE_PAGE), qint64(mappedIndex >= 0 ? mappedIndex + 1 : 0));
    }
    return mapped;
}

// ---------------------------------------------------------- PDFDocumentToolResizePages

PDFPageGeometry::ResizeSettings PDFDocumentToolResizePages::getSettings(const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolGeometryHelper;

    PDFPageGeometry::ResizeSettings settings;
    const QString size = parameters.value(Helper::name(PARAMETER_PAGE_SIZE)).toString();
    if (size == QLatin1String(SIZE_CUSTOM))
    {
        settings.pageSizeMM = QSizeF(Helper::readReal(parameters, PARAMETER_WIDTH), Helper::readReal(parameters, PARAMETER_HEIGHT));
        settings.isCustomSize = true;
    }
    else if (const std::optional<QSizeF> preset = PDFPageSizePresets::getSize(size))
    {
        settings.pageSizeMM = QSizeF(PDFPageSizePresets::getMillimeters(preset->width()), PDFPageSizePresets::getMillimeters(preset->height()));
    }

    const QString orientation = parameters.value(Helper::name(PARAMETER_ORIENTATION)).toString();
    if (orientation == QLatin1String(ORIENTATION_PORTRAIT))
    {
        settings.orientation = PDFPageGeometry::Orientation::Portrait;
    }
    else if (orientation == QLatin1String(ORIENTATION_LANDSCAPE))
    {
        settings.orientation = PDFPageGeometry::Orientation::Landscape;
    }

    settings.scaleContent = parameters.value(Helper::name(PARAMETER_CONTENT)).toString() != QLatin1String(CONTENT_KEEP_SIZE);
    settings.anchor = PDFDocumentToolGeometry::getAnchor(parameters.value(Helper::name(PARAMETER_ANCHOR)).toString());
    settings.marginsMM = QMarginsF(Helper::readReal(parameters, PARAMETER_MARGIN_LEFT), Helper::readReal(parameters, PARAMETER_MARGIN_TOP),
                                   Helper::readReal(parameters, PARAMETER_MARGIN_RIGHT), Helper::readReal(parameters, PARAMETER_MARGIN_BOTTOM));
    settings.offsetMM = QPointF(Helper::readReal(parameters, PARAMETER_OFFSET_X), Helper::readReal(parameters, PARAMETER_OFFSET_Y));
    settings.preserveAspectRatio = parameters.value(Helper::name(PARAMETER_PRESERVE_ASPECT_RATIO), true).toBool();
    settings.scaleAnnotations = parameters.value(Helper::name(PARAMETER_SCALE_ANNOTATIONS), true).toBool();
    return settings;
}

QString PDFDocumentToolResizePages::getPaperName(const QSizeF& sizeInPoints)
{
    // The tolerance of a whole millimeter: Letter is 215.9 x 279.4 mm
    const QString id = PDFPageSizePresets::findPreset(sizeInPoints, 1.0);
    if (id == QLatin1String(PDFPageSizePresets::A3))
    {
        return tr("A3");
    }
    if (id == QLatin1String(PDFPageSizePresets::A4))
    {
        return tr("A4");
    }
    if (id == QLatin1String(PDFPageSizePresets::A5))
    {
        return tr("A5");
    }
    if (id == QLatin1String(PDFPageSizePresets::LETTER))
    {
        return tr("Letter");
    }
    if (id == QLatin1String(PDFPageSizePresets::LEGAL))
    {
        return tr("Legal");
    }
    return QString();
}

QString PDFDocumentToolResizePages::getMarginsErrorText(const PDFPage* page, const QVariantMap& parameters)
{
    const auto format = PDFDocumentToolNewDocument::formatMillimeters;
    const PDFPageGeometry::ResizeSettings settings = getSettings(parameters);
    const PDFPageGeometry::ResizePlacement placement = PDFPageGeometry::computeResize(page, settings);
    if (placement.isValid || placement.sheetSize.isEmpty())
    {
        return placement.errorMessage;
    }

    // 391:25272: the sheet in millimeters and its name
    const PDFReal factor = page->getMillimetersToUnits();
    const QSizeF sheetMM(placement.sheetSize.width() / factor, placement.sheetSize.height() / factor);
    const QString paper = getPaperName(QSizeF(PDFPageSizePresets::getPoints(sheetMM.width()), PDFPageSizePresets::getPoints(sheetMM.height())));
    const double horizontal = settings.marginsMM.left() + settings.marginsMM.right();
    if (horizontal >= sheetMM.width())
    {
        return paper.isEmpty() ? tr("The left and right margins together (%1 mm) are wider than the page (%2 mm). Reduce the margins or choose a larger size.").arg(format(horizontal), format(sheetMM.width()))
                               : tr("The left and right margins together (%1 mm) are wider than the %2 page (%3 mm). Reduce the margins or choose a larger size.").arg(format(horizontal), paper, format(sheetMM.width()));
    }
    const double vertical = settings.marginsMM.top() + settings.marginsMM.bottom();
    return paper.isEmpty() ? tr("The top and bottom margins together (%1 mm) are higher than the page (%2 mm). Reduce the margins or choose a larger size.").arg(format(vertical), format(sheetMM.height()))
                           : tr("The top and bottom margins together (%1 mm) are higher than the %2 page (%3 mm). Reduce the margins or choose a larger size.").arg(format(vertical), paper, format(sheetMM.height()));
}

std::optional<std::vector<PDFPageGeometryChange>> PDFDocumentToolResizePages::createChanges(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
{
    using Helper = PDFDocumentToolGeometryHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    const std::vector<PDFInteger> pages = PDFDocumentToolGeometry::getPages(document, parameters, &error);
    if (pages.empty())
    {
        return std::nullopt;
    }

    const PDFCatalog* catalog = document->getCatalog();
    const PDFPageGeometry::ResizeSettings settings = getSettings(parameters);
    std::vector<PDFPageGeometryChange> changes;
    changes.reserve(pages.size());
    for (const PDFInteger pageIndex : pages)
    {
        const PDFPage* page = catalog->getPage(size_t(pageIndex));
        PDFPageGeometry::ResizePlacement placement = PDFPageGeometry::computeResize(page, settings, &document->getStorage());
        if (!placement.isValid)
        {
            error = getMarginsErrorText(page, parameters);
            if (pages.size() > 1)
            {
                error = tr("Page %1: %2").arg(pageIndex + 1).arg(error);
            }
            return std::nullopt;
        }

        // A page, which would stay as it is, is no change
        PDFPageGeometryChange& change = placement.change;
        const bool isSame = !change.hasContentTransform() &&
                            (!change.mediaBox || Helper::isSame(*change.mediaBox, page->getMediaBox())) &&
                            (!change.cropBox || Helper::isSame(*change.cropBox, page->getCropBox()));
        if (isSame)
        {
            continue;
        }
        change.pageIndex = pageIndex;
        changes.push_back(std::move(change));
    }
    return changes;
}

QString PDFDocumentToolResizePages::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolResizePages::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolGeometryHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, Helper::name(PARAMETER_PAGES), QT_TR_NOOP("Pages to resize"), QT_TR_NOOP("The pages, which get the new size")).setStored(false));

    // 391:23821: the list and the texts of the frame (K16), the values of PDFPageSizePresets
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_PAGE_SIZE), QT_TR_NOOP("Size"), QT_TR_NOOP("The new size of the pages"),
                                                 {
                                                     { QString::fromLatin1(SIZE_KEEP), QT_TR_NOOP("Keep page size"), QT_TR_NOOP("Only margins and placement change"), QStringLiteral("file-text") },
                                                     { QString::fromLatin1(PDFPageSizePresets::A5), QT_TR_NOOP("A5"), QT_TR_NOOP("148 × 210 mm"), QStringLiteral("file") },
                                                     { QString::fromLatin1(PDFPageSizePresets::A4), QT_TR_NOOP("A4"), QT_TR_NOOP("210 × 297 mm"), QStringLiteral("file") },
                                                     { QString::fromLatin1(PDFPageSizePresets::A3), QT_TR_NOOP("A3"), QT_TR_NOOP("297 × 420 mm"), QStringLiteral("file") },
                                                     { QString::fromLatin1(PDFPageSizePresets::LETTER), QT_TR_NOOP("Letter"), QT_TR_NOOP("216 × 279 mm"), QStringLiteral("file") },
                                                     { QString::fromLatin1(PDFPageSizePresets::LEGAL), QT_TR_NOOP("Legal"), QT_TR_NOOP("216 × 356 mm"), QStringLiteral("file") },
                                                     { QString::fromLatin1(SIZE_CUSTOM), QT_TR_NOOP("Custom"), QT_TR_NOOP("Width and height appear in the right panel"), QStringLiteral("sliders") },
                                                 },
                                                 QString::fromLatin1(PDFPageSizePresets::A4)));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_WIDTH, QT_TR_NOOP("Width"), QT_TR_NOOP("Width of the pages in millimeters as they are shown (pageSize custom)"), 210.0, PDFPageSizePresets::MINIMUM_SIDE_MILLIMETERS));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_HEIGHT, QT_TR_NOOP("Height"), QT_TR_NOOP("Height of the pages in millimeters as they are shown (pageSize custom)"), 297.0, PDFPageSizePresets::MINIMUM_SIDE_MILLIMETERS));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_ORIENTATION), QT_TR_NOOP("Orientation"), QT_TR_NOOP("How the new sheet lies (a preset size)"),
                                                 {
                                                     { QString::fromLatin1(ORIENTATION_AUTO), QT_TR_NOOP("Auto"), QT_TR_NOOP("Follows each page – a landscape page gets a landscape sheet"), QStringLiteral("orientation-auto") },
                                                     { QString::fromLatin1(ORIENTATION_PORTRAIT), QT_TR_NOOP("Portrait"), QT_TR_NOOP("Every page becomes upright"), QStringLiteral("file") },
                                                     { QString::fromLatin1(ORIENTATION_LANDSCAPE), QT_TR_NOOP("Landscape"), QT_TR_NOOP("Every page becomes wide"), QStringLiteral("file-wide") },
                                                 },
                                                 QString::fromLatin1(ORIENTATION_AUTO)));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_CONTENT), QT_TR_NOOP("Content"), QT_TR_NOOP("What happens with the content of the pages"),
                                                 {
                                                     { QString::fromLatin1(CONTENT_SCALE_TO_FIT), QT_TR_NOOP("Scale to fit"), QT_TR_NOOP("Content is enlarged or reduced to fit inside the margins"), QStringLiteral("scale") },
                                                     { QString::fromLatin1(CONTENT_KEEP_SIZE), QT_TR_NOOP("Keep size"), QT_TR_NOOP("Content is only placed by the anchor – nothing is rewritten"), QStringLiteral("file-text") },
                                                 },
                                                 QString::fromLatin1(CONTENT_SCALE_TO_FIT)));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_ANCHOR), QT_TR_NOOP("Anchor"), QT_TR_NOOP("Where the content sits inside the margins"),
                                                 {
                                                     { QStringLiteral("top-left"), QT_TR_NOOP("Top left") },
                                                     { QStringLiteral("top-center"), QT_TR_NOOP("Top center") },
                                                     { QStringLiteral("top-right"), QT_TR_NOOP("Top right") },
                                                     { QStringLiteral("middle-left"), QT_TR_NOOP("Middle left") },
                                                     { QStringLiteral("middle-center"), QT_TR_NOOP("Middle center") },
                                                     { QStringLiteral("middle-right"), QT_TR_NOOP("Middle right") },
                                                     { QStringLiteral("bottom-left"), QT_TR_NOOP("Bottom left") },
                                                     { QStringLiteral("bottom-center"), QT_TR_NOOP("Bottom center") },
                                                     { QStringLiteral("bottom-right"), QT_TR_NOOP("Bottom right") },
                                                 },
                                                 QStringLiteral("middle-center")));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_MARGIN_LEFT, QT_TR_NOOP("Left"), QT_TR_NOOP("Left margin of the new pages as they are shown, in millimeters"), 0.0, 0.0));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_MARGIN_TOP, QT_TR_NOOP("Top"), QT_TR_NOOP("Top margin of the new pages as they are shown, in millimeters"), 0.0, 0.0));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_MARGIN_RIGHT, QT_TR_NOOP("Right"), QT_TR_NOOP("Right margin of the new pages as they are shown, in millimeters"), 0.0, 0.0));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_MARGIN_BOTTOM, QT_TR_NOOP("Bottom"), QT_TR_NOOP("Bottom margin of the new pages as they are shown, in millimeters"), 0.0, 0.0));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_OFFSET_X, QT_TR_NOOP("Offset X"), QT_TR_NOOP("Moves the content to the right (negative: to the left), in millimeters"), 0.0, -Helper::MAXIMUM_LENGTH_MM));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_OFFSET_Y, QT_TR_NOOP("Offset Y"), QT_TR_NOOP("Moves the content down (negative: up), in millimeters"), 0.0, -Helper::MAXIMUM_LENGTH_MM));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_PRESERVE_ASPECT_RATIO), QT_TR_NOOP("Preserve aspect ratio"), QT_TR_NOOP("The content keeps its proportions when it is scaled"), true));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_SCALE_ANNOTATIONS), QT_TR_NOOP("Scale comments and form fields"),
                                                 QT_TR_NOOP("Comments and form fields are scaled with the content; otherwise they only move with it"), true));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolResizePages::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolGeometryHelper;

    std::vector<PDFDocumentToolParameterError> errors;
    const QString size = given.value(Helper::name(PARAMETER_PAGE_SIZE), QString::fromLatin1(PDFPageSizePresets::A4)).toString();
    const bool isCustom = size == QLatin1String(SIZE_CUSTOM);
    for (const char* parameter : { PARAMETER_WIDTH, PARAMETER_HEIGHT })
    {
        if (!isCustom && given.contains(Helper::name(parameter)))
        {
            errors.push_back({ Helper::name(parameter), tr("%1 is used only with pageSize custom. Leave it out, or set pageSize to custom.").arg(Helper::name(parameter)) });
        }
    }
    if ((isCustom || size == QLatin1String(SIZE_KEEP)) && given.contains(Helper::name(PARAMETER_ORIENTATION)))
    {
        errors.push_back({ Helper::name(PARAMETER_ORIENTATION), tr("orientation is used only with a named pageSize: custom and keep give the sheet as it is shown. Leave it out.") });
    }
    if (given.value(Helper::name(PARAMETER_CONTENT)).toString() == QLatin1String(CONTENT_KEEP_SIZE))
    {
        for (const char* parameter : { PARAMETER_PRESERVE_ASPECT_RATIO, PARAMETER_SCALE_ANNOTATIONS })
        {
            if (given.contains(Helper::name(parameter)))
            {
                errors.push_back({ Helper::name(parameter), tr("%1 is used only with content scale-to-fit: keep-size scales nothing. Leave it out.").arg(Helper::name(parameter)) });
            }
        }
    }
    return errors;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolResizePages::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolGeometryHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    QString error;
    const PDFDocument* document = context.getDocument();
    const std::optional<std::vector<PDFPageGeometryChange>> changes = createChanges(document, parameters, &error);
    if (!changes)
    {
        QString pagesError;
        const bool hasPages = !PDFDocumentToolGeometry::getPages(document, parameters, &pagesError).empty();
        errors.push_back({ Helper::name(hasPages ? PARAMETER_MARGIN_LEFT : PARAMETER_PAGES), error });
        return errors;
    }

    // R4, Table 22: a run, which moves or scales an annotation, needs bit 6
    const PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler();
    if (!PDFSecurityHandler::isAllowedByRevision(securityHandler, PDFSecurityHandler::Permission::ModifyInteractiveItems) &&
        PDFPageGeometry::isAnnotationChanged(document, *changes))
    {
        errors.push_back({ Helper::name(PARAMETER_CONTENT), tr("The permissions of the document do not allow moving its comments and form fields. Choose Keep size, or open the document with its owner password.") });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolResizePages::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(progress);
    using Helper = PDFDocumentToolGeometryHelper;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const PDFCatalog* catalog = document->getCatalog();

    QString error;
    const std::optional<std::vector<PDFPageGeometryChange>> changes = createChanges(document, parameters, &error);
    if (!changes)
    {
        return PDFDocumentToolResult::createFailed(error);
    }
    if (changes->empty())
    {
        return PDFDocumentToolResult::createNothingToDo(tr("The pages have this size already."));
    }

    PDFPageGeometryStatistics statistics;
    PDFDocument output = PDFPageGeometry::applyChanges(document, *changes, control, &statistics);
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    // The size of the first page (391:25948 "A4 · 210 × 297 mm (was Letter)") and the scales
    const PDFPageGeometry::ResizeSettings settings = getSettings(parameters);
    std::vector<PDFInteger> pages;
    int minimumPercent = std::numeric_limits<int>::max();
    int maximumPercent = 0;
    for (const PDFPageGeometryChange& change : *changes)
    {
        pages.push_back(change.pageIndex);
        const PDFPageGeometry::ResizePlacement placement = PDFPageGeometry::computeResize(catalog->getPage(size_t(change.pageIndex)), settings);
        const int percent = int(std::lround(qMin(placement.scaleX, placement.scaleY) * 100.0));
        minimumPercent = qMin(minimumPercent, percent);
        maximumPercent = qMax(maximumPercent, percent);
    }

    const PDFPage* firstBefore = catalog->getPage(size_t(pages.front()));
    const PDFPage* firstAfter = output.getCatalog()->getPage(size_t(pages.front()));
    const QSizeF sizeAfter = PDFPageGeometry::getShownSizeMM(firstAfter);
    const QSizeF sizeBefore = PDFPageGeometry::getShownSizeMM(firstBefore);
    const QString nameAfter = getPaperName(QSizeF(PDFPageSizePresets::getPoints(sizeAfter.width()), PDFPageSizePresets::getPoints(sizeAfter.height())));
    const QString nameBefore = getPaperName(QSizeF(PDFPageSizePresets::getPoints(sizeBefore.width()), PDFPageSizePresets::getPoints(sizeBefore.height())));
    const QString after = nameAfter.isEmpty() ? PDFDocumentToolGeometry::formatSizeMM(sizeAfter) : tr("%1 · %2").arg(nameAfter, PDFDocumentToolGeometry::formatSizeMM(sizeAfter));
    const QString before = nameBefore.isEmpty() ? PDFDocumentToolGeometry::formatSizeMM(sizeBefore) : nameBefore;

    QString content = tr("Not scaled");
    if (settings.scaleContent)
    {
        const QString percent = minimumPercent == maximumPercent ? tr("%1 %").arg(minimumPercent) : tr("%1–%2 %").arg(minimumPercent).arg(maximumPercent);
        content = statistics.annotationsTransformed > 0 && settings.scaleAnnotations ? tr("Scaled to %1, comments and fields too").arg(percent) : tr("Scaled to %1").arg(percent);
    }

    PDFDocumentToolResult result;
    result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), QLocale().toString(qint64(pages.size())) });
    result.facts.push_back({ QStringLiteral("pageSize"), tr("Page size"), tr("%1 (was %2)").arg(after, before) });
    result.facts.push_back({ QStringLiteral("content"), tr("Content"), content });

    result.data.insert(QStringLiteral("resizedCount"), qint64(pages.size()));
    result.data.insert(QStringLiteral("resizedPages"), Helper::getPageNumbers(pages));
    result.data.insert(QStringLiteral("scalePercent"), settings.scaleContent ? minimumPercent : 100);
    result.data.insert(QStringLiteral("annotationsTransformed"), statistics.annotationsTransformed);
    result.data.insert(QStringLiteral("destinationsTransformed"), statistics.destinationsTransformed);
    result.data.insert(QStringLiteral("viewportsTransformed"), statistics.viewportsTransformed);
    result.data.insert(QStringLiteral("beadsTransformed"), statistics.beadsTransformed);
    result.data.insert(QStringLiteral("patternsTransformed"), statistics.patternsTransformed);

    PDFModifiedDocument::ModificationFlags flags = PDFModifiedDocument::Reset;
    if (statistics.contentsTransformed > 0)
    {
        flags |= PDFModifiedDocument::PageContents;
    }
    if (statistics.annotationsTransformed > 0)
    {
        flags |= PDFModifiedDocument::Annotation;
    }
    Helper::addDocument(&result, input, std::move(output), QStringLiteral("_resized"), flags);
    return result;
}

QVariantMap PDFDocumentToolResizePages::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    if (!from || !to || from == to)
    {
        return parameters;
    }

    QVariantMap mapped = parameters;
    PDFDocumentToolGeometryHelper::mapSelection(&mapped, PARAMETER_PAGES, from, PDFDocumentToolPageSelection::mapPageIndices(from, to));
    return mapped;
}

// --------------------------------------------------------- PDFDocumentToolSetPageBoxes

std::optional<std::vector<PDFPageGeometry::Box>> PDFDocumentToolSetPageBoxes::parseWriteTo(const QString& text, QString* errorMessage)
{
    std::set<PDFPageGeometry::Box> boxes;
    for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QString boxName = part.trimmed();
        if (boxName.isEmpty())
        {
            continue;
        }
        const std::optional<PDFPageGeometry::Box> box = PDFPageGeometry::getBoxByName(boxName);
        if (!box)
        {
            if (errorMessage)
            {
                *errorMessage = tr("“%1” is no page box. Write the boxes separated by commas: media, crop, bleed, trim, art.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(boxName));
            }
            return std::nullopt;
        }
        boxes.insert(*box);
    }

    std::vector<PDFPageGeometry::Box> result;
    for (const PDFPageGeometry::Box box : PDFPageGeometry::getAllBoxes())
    {
        if (boxes.count(box))
        {
            result.push_back(box);
        }
    }
    return result;
}

QString PDFDocumentToolSetPageBoxes::createWriteTo(const std::vector<PDFPageGeometry::Box>& boxes)
{
    QStringList names;
    for (const PDFPageGeometry::Box box : PDFPageGeometry::getAllBoxes())
    {
        if (std::find(boxes.cbegin(), boxes.cend(), box) != boxes.cend())
        {
            names << PDFPageGeometry::getBoxName(box);
        }
    }
    return names.join(QLatin1Char(','));
}

QString PDFDocumentToolSetPageBoxes::getBoxTitle(PDFPageGeometry::Box box)
{
    switch (box)
    {
        case PDFPageGeometry::Box::MediaBox:
            return tr("Media box");
        case PDFPageGeometry::Box::CropBox:
            return tr("Crop box");
        case PDFPageGeometry::Box::BleedBox:
            return tr("Bleed box");
        case PDFPageGeometry::Box::TrimBox:
            return tr("Trim box");
        case PDFPageGeometry::Box::ArtBox:
            return tr("Art box");
    }
    return QString();
}

QString PDFDocumentToolSetPageBoxes::getBoxShortTitle(PDFPageGeometry::Box box)
{
    switch (box)
    {
        case PDFPageGeometry::Box::MediaBox:
            return tr("Media");
        case PDFPageGeometry::Box::CropBox:
            return tr("Crop");
        case PDFPageGeometry::Box::BleedBox:
            return tr("Bleed");
        case PDFPageGeometry::Box::TrimBox:
            return tr("Trim");
        case PDFPageGeometry::Box::ArtBox:
            return tr("Art");
    }
    return QString();
}

QMarginsF PDFDocumentToolSetPageBoxes::getMargins(const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolGeometryHelper;
    return QMarginsF(Helper::readReal(parameters, PARAMETER_MARGIN_LEFT), Helper::readReal(parameters, PARAMETER_MARGIN_TOP),
                     Helper::readReal(parameters, PARAMETER_MARGIN_RIGHT), Helper::readReal(parameters, PARAMETER_MARGIN_BOTTOM));
}

QRectF PDFDocumentToolSetPageBoxes::computeNewBox(const PDFPage* page, const QVariantMap& parameters, bool* isOutsideMedia)
{
    using Helper = PDFDocumentToolGeometryHelper;

    const std::optional<PDFPageGeometry::Box> basedOn = PDFPageGeometry::getBoxByName(parameters.value(Helper::name(PARAMETER_BASED_ON)).toString());
    const QRectF box = PDFPageGeometry::computeBox(page, basedOn.value_or(PDFPageGeometry::Box::TrimBox), getMargins(parameters));
    if (isOutsideMedia)
    {
        // 391:27638: Write to with the media box makes the sheet larger itself
        const std::optional<std::vector<PDFPageGeometry::Box>> writeTo = parseWriteTo(parameters.value(Helper::name(PARAMETER_WRITE_TO)).toString(), nullptr);
        const bool writesMedia = writeTo && std::find(writeTo->cbegin(), writeTo->cend(), PDFPageGeometry::Box::MediaBox) != writeTo->cend();
        const bool writesOther = writeTo && std::any_of(writeTo->cbegin(), writeTo->cend(), [](PDFPageGeometry::Box written) { return written != PDFPageGeometry::Box::MediaBox; });
        const QRectF media = page->getMediaBox().normalized();
        constexpr PDFReal tolerance = 1e-3;
        *isOutsideMedia = !writesMedia && writesOther && box.isValid() && !media.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(box);
    }
    return box;
}

PDFReal PDFDocumentToolSetPageBoxes::getLargestMarginInsideMedia(const PDFPage* page, const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolGeometryHelper;

    const std::optional<PDFPageGeometry::Box> basedOn = PDFPageGeometry::getBoxByName(parameters.value(Helper::name(PARAMETER_BASED_ON)).toString());
    const QRectF box = PDFPageGeometry::getEffectiveBox(page, basedOn.value_or(PDFPageGeometry::Box::TrimBox));
    const QRectF media = page->getMediaBox().normalized();
    const PDFReal smallest = qMin(qMin(box.left() - media.left(), media.right() - box.right()), qMin(box.top() - media.top(), media.bottom() - box.bottom()));
    // Tenths of a millimeter, rounded down (a written box may differ by the rounding of its numbers)
    return std::floor(smallest / page->getMillimetersToUnits() * 10.0 + 1e-3) / 10.0;
}

std::optional<std::vector<PDFPageGeometryChange>> PDFDocumentToolSetPageBoxes::createChanges(const PDFDocument* document, const QVariantMap& parameters, std::vector<PDFInteger>* outsideMediaPages, QString* errorMessage)
{
    using Helper = PDFDocumentToolGeometryHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    const std::vector<PDFInteger> pages = PDFDocumentToolGeometry::getPages(document, parameters, &error);
    if (pages.empty())
    {
        return std::nullopt;
    }

    const std::optional<std::vector<PDFPageGeometry::Box>> writeTo = parseWriteTo(parameters.value(Helper::name(PARAMETER_WRITE_TO)).toString(), &error);
    if (!writeTo)
    {
        return std::nullopt;
    }
    if (writeTo->empty())
    {
        // 391:28085
        error = tr("No box is selected. Choose at least one box in Write to – otherwise nothing would change.");
        return std::nullopt;
    }

    const PDFCatalog* catalog = document->getCatalog();
    std::vector<PDFPageGeometryChange> changes;
    for (const PDFInteger pageIndex : pages)
    {
        const PDFPage* page = catalog->getPage(size_t(pageIndex));
        bool isOutsideMedia = false;
        const QRectF box = computeNewBox(page, parameters, &isOutsideMedia);
        if (!PDFPageGeometry::isValidBox(box))
        {
            error = tr("The new box of page %1 would be smaller than 3 points or larger than 14,400 points. Choose other margins.").arg(pageIndex + 1);
            return std::nullopt;
        }
        if (isOutsideMedia && outsideMediaPages)
        {
            outsideMediaPages->push_back(pageIndex);
        }

        PDFPageGeometryChange change;
        change.pageIndex = pageIndex;
        bool isChanged = false;
        for (const PDFPageGeometry::Box written : *writeTo)
        {
            const QRectF current = PDFPageGeometry::getEffectiveBox(page, written);
            isChanged = isChanged || !Helper::isSame(current, box) || !PDFPageGeometry::isBoxDefined(page, &document->getStorage(), written);
            switch (written)
            {
                case PDFPageGeometry::Box::MediaBox:
                    change.mediaBox = box;
                    break;
                case PDFPageGeometry::Box::CropBox:
                    change.cropBox = box;
                    break;
                case PDFPageGeometry::Box::BleedBox:
                    change.bleedBox = box;
                    break;
                case PDFPageGeometry::Box::TrimBox:
                    change.trimBox = box;
                    break;
                case PDFPageGeometry::Box::ArtBox:
                    change.artBox = box;
                    break;
            }
        }
        if (isChanged)
        {
            changes.push_back(std::move(change));
        }
    }
    return changes;
}

QString PDFDocumentToolSetPageBoxes::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolSetPageBoxes::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolGeometryHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, Helper::name(PARAMETER_PAGES), QT_TR_NOOP("Pages"), QT_TR_NOOP("The pages, whose boxes are written")).setStored(false));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_BASED_ON), QT_TR_NOOP("Based on"), QT_TR_NOOP("The box, from which the new box is measured"),
                                                 {
                                                     { QStringLiteral("media"), QT_TR_NOOP("Media box"), QT_TR_NOOP("The whole sheet") },
                                                     { QStringLiteral("crop"), QT_TR_NOOP("Crop box"), QT_TR_NOOP("What viewers show and print") },
                                                     { QStringLiteral("bleed"), QT_TR_NOOP("Bleed box"), QT_TR_NOOP("Trim plus the bleed area") },
                                                     { QStringLiteral("trim"), QT_TR_NOOP("Trim box"), QT_TR_NOOP("The finished page after cutting") },
                                                     { QStringLiteral("art"), QT_TR_NOOP("Art box"), QT_TR_NOOP("The meaningful content") },
                                                 },
                                                 QStringLiteral("trim")));
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_WRITE_TO), QT_TR_NOOP("Write to"),
                                                   QT_TR_NOOP("The boxes, which get the new box, separated by commas: media, crop, bleed, trim, art (every checked box gets the same rectangle)"),
                                                   QStringLiteral("bleed"), 64));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_MARGIN_LEFT, QT_TR_NOOP("Left"), QT_TR_NOOP("Left side of the new box from the left side of the box Based on as the page is shown, in millimeters; positive makes the box larger, negative smaller"), 3.0, -Helper::MAXIMUM_LENGTH_MM));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_MARGIN_TOP, QT_TR_NOOP("Top"), QT_TR_NOOP("Top side of the new box from the top of the box Based on, in millimeters; positive makes the box larger, negative smaller"), 3.0, -Helper::MAXIMUM_LENGTH_MM));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_MARGIN_RIGHT, QT_TR_NOOP("Right"), QT_TR_NOOP("Right side of the new box from the right side of the box Based on, in millimeters; positive makes the box larger, negative smaller"), 3.0, -Helper::MAXIMUM_LENGTH_MM));
    definitions.push_back(Helper::createMargin(tr, PARAMETER_MARGIN_BOTTOM, QT_TR_NOOP("Bottom"), QT_TR_NOOP("Bottom side of the new box from the bottom of the box Based on, in millimeters; positive makes the box larger, negative smaller"), 3.0, -Helper::MAXIMUM_LENGTH_MM));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolSetPageBoxes::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolGeometryHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    QString error;
    if (!createChanges(context.getDocument(), parameters, nullptr, &error))
    {
        QString pagesError;
        const bool hasPages = !PDFDocumentToolGeometry::getPages(context.getDocument(), parameters, &pagesError).empty();
        QString writeToError;
        const std::optional<std::vector<PDFPageGeometry::Box>> writeTo = parseWriteTo(parameters.value(Helper::name(PARAMETER_WRITE_TO)).toString(), &writeToError);
        const char* parameter = !hasPages ? PARAMETER_PAGES : (!writeTo || writeTo->empty() ? PARAMETER_WRITE_TO : PARAMETER_MARGIN_LEFT);
        errors.push_back({ Helper::name(parameter), error });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolSetPageBoxes::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(progress);
    using Helper = PDFDocumentToolGeometryHelper;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const PDFCatalog* catalog = document->getCatalog();

    QString error;
    std::vector<PDFInteger> outsideMediaPages;
    const std::optional<std::vector<PDFPageGeometryChange>> changes = createChanges(document, parameters, &outsideMediaPages, &error);
    if (!changes)
    {
        return PDFDocumentToolResult::createFailed(error);
    }
    if (changes->empty())
    {
        return PDFDocumentToolResult::createNothingToDo(tr("The boxes of the pages have these values already."));
    }

    PDFPageGeometryStatistics statistics;
    PDFDocument output = PDFPageGeometry::applyChanges(document, *changes, control, &statistics);
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    std::vector<PDFInteger> pages;
    for (const PDFPageGeometryChange& change : *changes)
    {
        pages.push_back(change.pageIndex);
    }

    // 391:28535 "Bleed box: 216 × 303 mm (trim box + 3 mm)" of the first page
    const std::optional<PDFPageGeometry::Box> basedOn = PDFPageGeometry::getBoxByName(parameters.value(Helper::name(PARAMETER_BASED_ON)).toString());
    const QString basedOnTitle = getBoxTitle(basedOn.value_or(PDFPageGeometry::Box::TrimBox)).toLower();
    const QMarginsF margins = getMargins(parameters);
    const auto format = PDFDocumentToolNewDocument::formatMillimeters;
    QString marginsText;
    if (qFuzzyCompare(margins.left(), margins.top()) && qFuzzyCompare(margins.left(), margins.right()) && qFuzzyCompare(margins.left(), margins.bottom()))
    {
        marginsText = margins.left() < 0.0 ? tr("%1 − %2 mm").arg(basedOnTitle, format(-margins.left())) : tr("%1 + %2 mm").arg(basedOnTitle, format(margins.left()));
    }
    else
    {
        marginsText = tr("%1 with the margins %2, %3, %4, %5 mm").arg(basedOnTitle, format(margins.left()), format(margins.top()), format(margins.right()), format(margins.bottom()));
    }

    const PDFPage* firstPage = catalog->getPage(size_t(pages.front()));
    const QRectF newBox = computeNewBox(firstPage, parameters, nullptr);
    const QString sizeText = PDFDocumentToolGeometry::formatSizeMM(Helper::getShownSizeMM(firstPage, newBox));
    const std::optional<std::vector<PDFPageGeometry::Box>> writeTo = parseWriteTo(parameters.value(Helper::name(PARAMETER_WRITE_TO)).toString(), nullptr);

    PDFDocumentToolResult result;
    result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), QLocale().toString(qint64(pages.size())) });
    QJsonArray writtenBoxes;
    for (const PDFPageGeometry::Box box : writeTo.value_or(std::vector<PDFPageGeometry::Box>()))
    {
        result.facts.push_back({ PDFPageGeometry::getBoxName(box) + QStringLiteral("Box"), getBoxTitle(box), tr("%1 (%2)").arg(sizeText, marginsText) });
        writtenBoxes.append(PDFPageGeometry::getBoxName(box));
    }
    result.facts.push_back({ QStringLiteral("content"), tr("Content"), tr("Not changed") });
    if (!outsideMediaPages.empty())
    {
        // 391:27638: written as it is; viewers and printers reduce it to the media box (14.11.2)
        result.facts.push_back({ QStringLiteral("outsideMedia"), tr("Outside the media box"), tr("%Ln pages · viewers and printers clip the box", nullptr, int(outsideMediaPages.size())) });
    }

    result.data.insert(QStringLiteral("changedCount"), qint64(pages.size()));
    result.data.insert(QStringLiteral("changedPages"), Helper::getPageNumbers(pages));
    result.data.insert(QStringLiteral("basedOn"), PDFPageGeometry::getBoxName(basedOn.value_or(PDFPageGeometry::Box::TrimBox)));
    result.data.insert(QStringLiteral("writeTo"), writtenBoxes);
    result.data.insert(QStringLiteral("outsideMediaPages"), Helper::getPageNumbers(outsideMediaPages));
    Helper::addDocument(&result, input, std::move(output), QStringLiteral("_boxes"), PDFModifiedDocument::Reset);
    return result;
}

QVariantMap PDFDocumentToolSetPageBoxes::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    if (!from || !to || from == to)
    {
        return parameters;
    }

    QVariantMap mapped = parameters;
    PDFDocumentToolGeometryHelper::mapSelection(&mapped, PARAMETER_PAGES, from, PDFDocumentToolPageSelection::mapPageIndices(from, to));
    return mapped;
}

}   // namespace pdf
