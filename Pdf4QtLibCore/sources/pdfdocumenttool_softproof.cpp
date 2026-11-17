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

#include "pdfdocumenttool_softproof.h"
#include "pdfdocumenttool_pdftoimages.h"
#include "pdfoutputpreviewanalyzer.h"

#include "pdfcatalog.h"
#include "pdfconstants.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpage.h"
#include "pdfrenderer.h"

#include <QJsonArray>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <cmath>
#include <map>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helpers of the tool Soft Proofing
class PDFDocumentToolSoftProofHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolSoftProof)

public:
    PDFDocumentToolSoftProofHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// The measure of one page
    struct PageMeasure
    {
        PDFInteger pageIndex = -1;
        bool isMeasured = false;    ///< Both images exist (else the page is left out with an issue)
        double share = 0.0;         ///< % of the pixels of the crop box
        double area = 0.0;          ///< mm²
    };

    /// The pages of the parameter "pages" (empty = all pages); empty and the reason, when there is none
    static std::vector<PDFInteger> getPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
    {
        const QVariant value = parameters.value(name(PDFDocumentToolSoftProof::PARAMETER_PAGES));
        const std::optional<PDFDocumentToolPageSelection> selection = value.isValid() ? PDFDocumentToolPageSelection::fromVariant(value, errorMessage)
                                                                                     : std::optional<PDFDocumentToolPageSelection>(PDFDocumentToolPageSelection());
        if (!selection)
        {
            return {};
        }
        std::vector<PDFInteger> pages = selection->getPageIndices(document, errorMessage);
        if (pages.empty() && errorMessage->isEmpty())
        {
            *errorMessage = tr("No page is selected.");
        }
        return pages;
    }

    /// The image of a page: the rotated media box (the renderer maps it onto the whole image), its longer side
    /// \p resolution pixels; an empty size for a page without an area
    static QSize getImageSize(const PDFPage* page, int resolution)
    {
        const QRectF mediaBox = page->getRotatedMediaBox();
        const qreal longerSide = qMax(mediaBox.width(), mediaBox.height());
        if (!std::isfinite(longerSide) || longerSide <= 0.0 || mediaBox.width() <= 0.0 || mediaBox.height() <= 0.0)
        {
            return QSize();
        }
        const qreal scale = qreal(resolution) / longerSide;
        return QSize(qBound(1, qRound(mediaBox.width() * scale), resolution), qBound(1, qRound(mediaBox.height() * scale), resolution));
    }

    /// The pixels of the crop box of the page in its image (ISO 32000-2, 14.11.2: the visible page)
    static QRect getCropRect(const PDFPage* page, const QSize& imageSize)
    {
        const QRect imageRect(QPoint(0, 0), imageSize);
        const QTransform matrix = PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(imageRect));
        return matrix.mapRect(page->getEffectiveCropBox()).toAlignedRect().intersected(imageRect);
    }

    /// Measures a page: \p white has the alarm color white, \p black black, otherwise they are the same
    /// rendering. A pixel covered by a color out of the gamut by the fraction a differs by a x 255.
    static PageMeasure measure(const PDFPage* page, PDFInteger pageIndex, const QImage& white, const QImage& black)
    {
        PageMeasure result;
        result.pageIndex = pageIndex;
        if (white.isNull() || black.isNull() || white.size() != black.size())
        {
            return result;
        }

        const QImage whiteImage = white.convertToFormat(QImage::Format_ARGB32);
        const QImage blackImage = black.convertToFormat(QImage::Format_ARGB32);
        const QRect cropRect = getCropRect(page, whiteImage.size());
        const qint64 pixelCount = qint64(cropRect.width()) * qint64(cropRect.height());
        if (pixelCount <= 0)
        {
            return result;
        }

        double coverage = 0.0;
        for (int y = cropRect.top(); y <= cropRect.bottom(); ++y)
        {
            const QRgb* whiteLine = reinterpret_cast<const QRgb*>(whiteImage.constScanLine(y));
            const QRgb* blackLine = reinterpret_cast<const QRgb*>(blackImage.constScanLine(y));
            qint64 lineDifference = 0;
            for (int x = cropRect.left(); x <= cropRect.right(); ++x)
            {
                const QRgb whitePixel = whiteLine[x];
                const QRgb blackPixel = blackLine[x];
                if (whitePixel != blackPixel)
                {
                    const int difference = (qRed(whitePixel) - qRed(blackPixel)) + (qGreen(whitePixel) - qGreen(blackPixel)) + (qBlue(whitePixel) - qBlue(blackPixel));
                    lineDifference += qMax(difference, 0);
                }
            }
            coverage += double(lineDifference) / (3.0 * 255.0);
        }

        // The area of a pixel: the media box of the page in millimeters (/UserUnit, Table 31) over its image
        const QRectF mediaBox = page->getRotatedMediaBox();
        const double millimetersPerUnit = page->getEffectiveUserUnit() * 25.4 / 72.0;
        const double pixelWidth = mediaBox.width() * millimetersPerUnit / double(whiteImage.width());
        const double pixelHeight = mediaBox.height() * millimetersPerUnit / double(whiteImage.height());

        result.isMeasured = true;
        result.share = qBound(0.0, 100.0 * coverage / double(pixelCount), 100.0);
        result.area = coverage * pixelWidth * pixelHeight;
        return result;
    }

    /// A number with at most \p decimals decimals for the data (a stable value for MCP)
    static double round(double value, int decimals)
    {
        const double factor = std::pow(10.0, decimals);
        return std::round(value * factor) / factor;
    }

    /// The identifier of a rendering intent
    static QString getIntentId(const QVariantMap& parameters)
    {
        return parameters.value(name(PDFDocumentToolSoftProof::PARAMETER_INTENT), QString::fromLatin1(PDFDocumentToolSoftProof::INTENT_RELATIVE_COLORIMETRIC)).toString();
    }
};

QString PDFDocumentToolSoftProof::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolSoftProof::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_PROFILE), QT_TR_NOOP("Profile"),
                                                   QT_TR_NOOP("The CMYK profile of the printing; empty = the output intent of the document"), QString(), 1024));

    std::vector<PDFDocumentToolParameterOption> intents;
    intents.push_back({ QString::fromLatin1(INTENT_AUTO), QT_TR_NOOP("Auto"), QT_TR_NOOP("The intent of each object of the document") });
    intents.push_back({ QString::fromLatin1(INTENT_PERCEPTUAL), QT_TR_NOOP("Perceptual") });
    intents.push_back({ QString::fromLatin1(INTENT_RELATIVE_COLORIMETRIC), QT_TR_NOOP("Relative colorimetric") });
    intents.push_back({ QString::fromLatin1(INTENT_SATURATION), QT_TR_NOOP("Saturation") });
    intents.push_back({ QString::fromLatin1(INTENT_ABSOLUTE_COLORIMETRIC), QT_TR_NOOP("Absolute colorimetric") });
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_INTENT), QT_TR_NOOP("Rendering intent"), QT_TR_NOOP("How the colors are converted for the proof"),
                                                 std::move(intents), QString::fromLatin1(INTENT_RELATIVE_COLORIMETRIC)));
    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_PAGES), QT_TR_NOOP("Pages"), QT_TR_NOOP("Pages, whose colors are checked")));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_RESOLUTION), QT_TR_NOOP("Resolution"), QT_TR_NOOP("Pixels of the longer side of a checked page"),
                                                    1024, 72, PDFOutputPreviewSettings::MAXIMUM_RESOLUTION));
    definitions.push_back(Definition::createColor(tr, QString::fromLatin1(PARAMETER_ALARM_COLOR), QT_TR_NOOP("Out of gamut color"), QT_TR_NOOP("The color, which marks the colors the profile cannot print"), QColor::fromRgb(DEFAULT_ALARM_COLOR)));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolSoftProof::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolSoftProofHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    const PDFDocument* document = context.getDocument();
    if (!errors.empty() || !document)
    {
        return errors;
    }

    QString errorMessage;
    const std::vector<PDFInteger> pages = Helper::getPages(document, parameters, &errorMessage);
    if (pages.empty())
    {
        errors.push_back({ Helper::name(PARAMETER_PAGES), errorMessage });
    }
    else if (pages.size() > size_t(MAXIMUM_PAGES))
    {
        // Plan 7.1: a run checks at most 50 pages (each is rendered twice)
        errors.push_back({ Helper::name(PARAMETER_PAGES), tr("Select at most %Ln pages – %1 are selected.", nullptr, MAXIMUM_PAGES).arg(QLocale().toString(qint64(pages.size()))) });
    }
    return errors;
}

RenderingIntent PDFDocumentToolSoftProof::getRenderingIntent(const QString& intent)
{
    if (intent == QLatin1String(INTENT_PERCEPTUAL))
    {
        return RenderingIntent::Perceptual;
    }
    if (intent == QLatin1String(INTENT_RELATIVE_COLORIMETRIC))
    {
        return RenderingIntent::RelativeColorimetric;
    }
    if (intent == QLatin1String(INTENT_SATURATION))
    {
        return RenderingIntent::Saturation;
    }
    if (intent == QLatin1String(INTENT_ABSOLUTE_COLORIMETRIC))
    {
        return RenderingIntent::AbsoluteColorimetric;
    }
    return RenderingIntent::Auto;
}

const PDFSoftProofingProfile* PDFDocumentToolSoftProof::findProfile(const PDFSoftProofingProfiles& profiles, const QString& profile, QString* errorMessage)
{
    if (profile.isEmpty())
    {
        // The output intent of the document (the first available one, PDFCMSSettings::softProofingProfile)
        const PDFSoftProofingProfile* unavailable = nullptr;
        for (const PDFSoftProofingProfile& item : profiles)
        {
            if (item.source != PDFSoftProofingProfile::Source::OutputIntent)
            {
                continue;
            }
            if (item.isAvailable)
            {
                return &item;
            }
            if (!unavailable)
            {
                unavailable = &item;
            }
        }

        if (unavailable)
        {
            *errorMessage = tr("The output intent %1 cannot be used: %2. Choose a CMYK profile.").arg(unavailable->name, unavailable->reason);
        }
        else
        {
            *errorMessage = tr("The document has no output intent. Choose a CMYK profile – the installed profiles and the profile folder are in Settings › Color management.");
        }
        return nullptr;
    }

    for (const PDFSoftProofingProfile& item : profiles)
    {
        if (item.isAvailable && item.id == profile)
        {
            return &item;
        }
    }

    *errorMessage = tr("\"%1\" is no CMYK profile of the output intents of the document, of the system or of the profile folder of Settings › Color management.").arg(profile);
    return nullptr;
}

PDFDocumentToolResult PDFDocumentToolSoftProof::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolSoftProofHelper;

    const PDFDocument* document = context.getDocument();
    if (!document)
    {
        return PDFDocumentToolResult::createFailed(tr("There is no document."));
    }

    QString errorMessage;
    const std::vector<PDFInteger> pages = Helper::getPages(document, parameters, &errorMessage);
    if (pages.empty())
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }
    if (pages.size() > size_t(MAXIMUM_PAGES))
    {
        return PDFDocumentToolResult::createFailed(tr("Select at most %Ln pages – %1 are selected.", nullptr, MAXIMUM_PAGES).arg(QLocale().toString(qint64(pages.size()))));
    }

    // The color management of the run: the settings of the context (the application), else the default
    // settings with the output intents of the document (MCP, PdfTool)
    const PDFDocumentToolPageRenderSettings renderSettings = PDFDocumentToolPageRenderSettings::create(context.rendering);
    PDFCMSManager profileManager(nullptr);
    profileManager.setDocument(document);
    const PDFCMSSettings baseSettings = renderSettings.cmsSettings ? *renderSettings.cmsSettings : profileManager.getDefaultSettings();
    if (baseSettings.system != PDFCMSSettings::System::LittleCMS2)
    {
        return PDFDocumentToolResult::createFailed(tr("Soft proofing needs the color management Little CMS 2. Choose it in Settings › Color management."));
    }
    profileManager.setSettings(baseSettings);

    const PDFSoftProofingProfiles profiles = profileManager.getSoftProofingProfiles();
    const PDFSoftProofingProfile* profile = findProfile(profiles, parameters.value(Helper::name(PARAMETER_PROFILE)).toString(), &errorMessage);
    if (!profile)
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    // The gamut check of the profile, no proof (the colors of the gamut stay as they are): the same settings
    // twice, only the alarm color differs - each manager makes its own color management with its own context
    // of Little CMS (B200), so the two alarm colors never mix
    const QString intentId = Helper::getIntentId(parameters);
    PDFCMSSettings gamutSettings = baseSettings.withoutProofing();
    gamutSettings.isGamutChecking = true;
    gamutSettings.softProofingProfile = profile->id;
    gamutSettings.proofingIntent = getRenderingIntent(intentId);

    PDFCMSSettings whiteSettings = gamutSettings;
    whiteSettings.outOfGamutColor = QColor(Qt::white);
    PDFCMSSettings blackSettings = gamutSettings;
    blackSettings.outOfGamutColor = QColor(Qt::black);

    // The objects of the rendering belong to this run and to its thread; the pools and their rasterizers
    // die at the end of this function, after the last thread of the pools has ended
    PDFCMSManager whiteManager(nullptr);
    whiteManager.setDocument(document);
    whiteManager.setSettings(whiteSettings);
    PDFCMSManager blackManager(nullptr);
    blackManager.setDocument(document);
    blackManager.setSettings(blackSettings);

    PDFOptionalContentActivity optionalContentActivity(document, renderSettings.layerStates ? OCUsage::View : OCUsage::Export, nullptr);
    if (renderSettings.layerStates)
    {
        for (const auto& [ocg, state] : *renderSettings.layerStates)
        {
            optionalContentActivity.setState(ocg, state, false);
        }
    }

    PDFMeshQualitySettings meshQualitySettings;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    const PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    const PDFRenderer::Features features = PDFDocumentToolPageRenderSettings::getOutputFeatures(renderSettings.features);
    const int rasterizerCount = PDFRasterizerPool::getCorrectedRasterizerCount(renderSettings.rasterizerCount > 0 ? renderSettings.rasterizerCount : PDFRasterizerPool::getDefaultRasterizerCount());
    PDFRasterizerPool whitePool(document, &fontCache, &iccProfileCache, &whiteManager, &optionalContentActivity, features, meshQualitySettings, rasterizerCount, renderSettings.engine, nullptr);
    PDFRasterizerPool blackPool(document, &fontCache, &iccProfileCache, &blackManager, &optionalContentActivity, features, meshQualitySettings, rasterizerCount, renderSettings.engine, nullptr);

    const int resolution = qBound(72, parameters.value(Helper::name(PARAMETER_RESOLUTION), 1024).toInt(), PDFOutputPreviewSettings::MAXIMUM_RESOLUTION);
    const PDFRasterizerPool::PageImageSizeGetter getImageSize = [resolution](const PDFPage* page)
    {
        return Helper::getImageSize(page, resolution);
    };

    // The state shared by the threads of the pools (guarded by the mutex): the first images of the group,
    // the measures and the issues of the pages
    QMutex mutex;
    std::map<PDFInteger, QImage> whiteImages;
    std::map<PDFInteger, Helper::PageMeasure> measures;
    std::map<PDFInteger, QStringList> issues;

    const PDFRasterizerPool::ProcessImageMethod storeWhite = [&](PDFRenderedPageImage& image)
    {
        // Each text once, by a set (a hostile page has millions of distinct errors: no search in the list)
        QStringList messages;
        QSet<QString> knownMessages;
        for (const PDFRenderError& error : image.getReportedErrors())
        {
            if (!knownMessages.contains(error.message))
            {
                knownMessages.insert(error.message);
                messages << error.message;
            }
        }

        QMutexLocker locker(&mutex);
        whiteImages[image.pageIndex] = std::move(image.pageImage);
        issues[image.pageIndex] = std::move(messages);
    };

    const PDFRasterizerPool::ProcessImageMethod measureBlack = [&](PDFRenderedPageImage& image)
    {
        QImage whiteImage;
        {
            QMutexLocker locker(&mutex);
            auto it = whiteImages.find(image.pageIndex);
            if (it != whiteImages.end())
            {
                whiteImage = std::move(it->second);
                whiteImages.erase(it);
            }
        }

        // The page objects and the document are immutable: read by more threads at once
        const PDFPage* page = document->getCatalog()->getPage(size_t(image.pageIndex));
        Helper::PageMeasure measure = Helper::measure(page, image.pageIndex, whiteImage, image.pageImage);

        {
            QMutexLocker locker(&mutex);
            measures[image.pageIndex] = measure;
        }

        if (progress)
        {
            progress->step();
        }
    };

    if (progress)
    {
        progress->start(pages.size(), ProgressStartupInfo());
    }

    // The groups of pages: both images of each page exist at once, the first images of a group are kept
    // until their second images are done (MAXIMUM_GROUP_IMAGE_BYTES, at most 2 x the rasterizers)
    size_t groupStart = 0;
    while (groupStart < pages.size() && !PDFOperationControl::isOperationCanceled(control))
    {
        std::vector<PDFInteger> group;
        qint64 groupBytes = 0;
        for (size_t i = groupStart; i < pages.size(); ++i)
        {
            const qint64 bytes = PDFRasterizerPool::getImageDataSize(getImageSize(document->getCatalog()->getPage(size_t(pages[i]))));
            if (!group.empty() && (groupBytes + bytes > MAXIMUM_GROUP_IMAGE_BYTES || group.size() >= size_t(2 * rasterizerCount)))
            {
                break;
            }
            group.push_back(pages[i]);
            groupBytes += bytes;
        }
        groupStart += group.size();

        whitePool.render(group, getImageSize, storeWhite, nullptr, control);
        blackPool.render(group, getImageSize, measureBlack, nullptr, control);

        QMutexLocker locker(&mutex);
        whiteImages.clear();
    }

    fontCache.setCacheShrinkEnabled(nullptr, true);

    if (PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    if (progress)
    {
        progress->finish();
    }

    // The data and the facts in the order of the pages
    PDFDocumentToolResult result;
    QJsonArray pagesData;
    int pagesOutOfGamut = 0;
    const Helper::PageMeasure* largest = nullptr;
    for (const PDFInteger pageIndex : pages)
    {
        auto it = measures.find(pageIndex);
        const bool isMeasured = it != measures.end() && it->second.isMeasured;
        for (const QString& message : issues[pageIndex])
        {
            result.renderingIssues.push_back({ pageIndex, message });
        }

        if (!isMeasured)
        {
            result.renderingIssues.push_back({ pageIndex, tr("The page could not be rendered – it is not checked.") });
            continue;
        }

        const Helper::PageMeasure& measure = it->second;
        QJsonObject pageData;
        pageData.insert(QString::fromLatin1(DATA_PAGE), qint64(pageIndex + 1));
        pageData.insert(QString::fromLatin1(DATA_SHARE), Helper::round(measure.share, 3));
        pageData.insert(QString::fromLatin1(DATA_AREA), Helper::round(measure.area, 2));
        pagesData.append(pageData);

        if (measure.share > 0.0)
        {
            ++pagesOutOfGamut;
        }
        if (!largest || measure.share > largest->share)
        {
            largest = &measure;
        }
    }

    if (pagesData.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(tr("No page could be rendered."));
    }

    result.data.insert(QString::fromLatin1(DATA_PAGES), pagesData);
    result.data.insert(QString::fromLatin1(DATA_PROFILE), profile->name);
    result.data.insert(QString::fromLatin1(DATA_PROFILE_ID), profile->id);
    result.data.insert(QString::fromLatin1(DATA_INTENT), intentId);
    result.data.insert(QString::fromLatin1(DATA_PAGES_OUT_OF_GAMUT), pagesOutOfGamut);

    // The facts are machine-readable (MCP, PdfTool): the C locale
    const QLocale locale = QLocale::c();
    result.facts.push_back({ QStringLiteral("profile"), tr("Profile"), profile->name });
    result.facts.push_back({ QStringLiteral("pagesOutOfGamut"), tr("Pages with colors out of gamut"), tr("%1 of %2").arg(locale.toString(pagesOutOfGamut), locale.toString(qint64(pagesData.size()))) });
    if (largest && largest->share > 0.0)
    {
        result.facts.push_back({ QStringLiteral("largestShare"), tr("Largest share"), tr("%1 % on page %2").arg(locale.toString(largest->share, 'f', 1), locale.toString(largest->pageIndex + 1)) });
    }
    return result;
}

QVariantMap PDFDocumentToolSoftProof::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    using Helper = PDFDocumentToolSoftProofHelper;

    if (!from || !to || from == to)
    {
        return parameters;
    }

    // A page named by its number follows its page object (as PDF to Images)
    QVariantMap mapped = parameters;
    const QString key = Helper::name(PARAMETER_PAGES);
    QString error;
    if (mapped.contains(key))
    {
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(mapped.value(key), &error);
        if (selection)
        {
            mapped.insert(key, selection->mapPages(from, PDFDocumentToolPageSelection::mapPageIndices(from, to)).toVariant());
        }
    }
    return mapped;
}

}   // namespace pdf
