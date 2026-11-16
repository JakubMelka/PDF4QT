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

#include "pdfredactionverifier.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenthiddendatascan.h"
#include "pdfdocumentreader.h"
#include "pdfencoding.h"
#include "pdfexecutionpolicy.h"
#include "pdffont.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdfpainterutils.h"
#include "pdfparser.h"
#include "pdfpathboolean.h"
#include "pdfpattern.h"
#include "pdfprogress.h"
#include "pdftextlayoutgenerator.h"
#include "pdfutils.h"

#include <QMutex>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// The parts of the check of a redacted copy (PDFRedactionVerifier)
class PDFRedactionVerifierHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFRedactionVerifier)

public:
    /// Shortest word of a redacted area looked for in the texts of the copy (6.8, step 3)
    static constexpr qsizetype MINIMUM_WORD_LENGTH = 4;

    /// Most polygons of the areas of a page grouped into areas with their holes (beyond it the
    /// page is one area)
    static constexpr qsizetype MAXIMUM_GROUPED_POLYGONS = 2000;

    /// Most page numbers named in a warning
    static constexpr size_t MAXIMUM_LISTED_PAGES = 10;

    /// One redacted area of a page with its holes, and the area shrunk by one pixel of the check
    struct Component
    {
        QPainterPath area;      ///< The area (Qt::WindingFill)
        QPainterPath shrunk;    ///< The area without the strip of one pixel at its boundary (empty = smaller than a pixel)
        QRectF shrunkBox;       ///< The box of shrunk
    };

    /// The state of the check shared by the pages (under the mutex)
    struct SharedState
    {
        QMutex mutex;
        std::vector<PDFRedactionVerifier::Finding> errors;
        std::set<QString> words;                    ///< Case-folded words of the redacted areas
        std::vector<PDFInteger> softMaskPages;      ///< Pages of the source with a soft mask
    };

    /// The width of the strip at the boundary of an area, which the check of the pixels skips
    static PDFReal getPixelSize() { return 72.0 / PDFRedactionVerifier::CHECK_DPI; }

    /// The areas of a page split into the areas with their holes (an outer boundary is inside of
    /// an even number of other boundaries, a hole of an odd number)
    static std::vector<Component> createComponents(const QPainterPath& path)
    {
        // The areas are regions of the non-zero rule: their concatenation is their union under it,
        // normalized by the clipper (the holes oriented against their outer boundaries)
        std::vector<Component> components;
        QPainterPath region = path;
        region.setFillRule(Qt::WindingFill);
        const QPainterPath united = region.isEmpty() ? QPainterPath() : PDFPathBoolean::unite(region, region);
        const QList<QPolygonF> polygons = united.toSubpathPolygons();
        if (polygons.isEmpty())
        {
            return components;
        }

        std::vector<QPainterPath> areas;
        if (polygons.size() > MAXIMUM_GROUPED_POLYGONS)
        {
            areas.push_back(united);
        }
        else
        {
            std::vector<int> depths(size_t(polygons.size()), 0);
            for (qsizetype i = 0; i < polygons.size(); ++i)
            {
                for (qsizetype j = 0; j < polygons.size(); ++j)
                {
                    if (i != j && !polygons[i].isEmpty() && polygons[j].containsPoint(polygons[i].front(), Qt::OddEvenFill))
                    {
                        ++depths[size_t(i)];
                    }
                }
            }

            for (qsizetype i = 0; i < polygons.size(); ++i)
            {
                if (depths[size_t(i)] % 2 != 0)
                {
                    continue;
                }

                QPainterPath area;
                area.setFillRule(Qt::WindingFill);
                area.addPolygon(polygons[i]);
                area.closeSubpath();
                for (qsizetype j = 0; j < polygons.size(); ++j)
                {
                    // A hole of this boundary: one level deeper and inside of it
                    if (depths[size_t(j)] == depths[size_t(i)] + 1 && !polygons[j].isEmpty() && polygons[i].containsPoint(polygons[j].front(), Qt::OddEvenFill))
                    {
                        area.addPolygon(polygons[j]);
                        area.closeSubpath();
                    }
                }
                areas.push_back(PDFPathBoolean::intersect(area, united));
            }
        }

        for (QPainterPath& area : areas)
        {
            Component component;
            QPainterPathStroker stroker;
            stroker.setWidth(2.0 * getPixelSize());
            stroker.setJoinStyle(Qt::MiterJoin);
            component.shrunk = PDFPathBoolean::subtract(area, stroker.createStroke(area));
            component.shrunkBox = component.shrunk.boundingRect();
            component.area = std::move(area);
            components.push_back(std::move(component));
        }
        return components;
    }

    /// The colors differ by at most COLOR_TOLERANCE in each channel
    static bool isNearColor(QRgb pixel, QRgb color)
    {
        return std::abs(qRed(pixel) - qRed(color)) <= PDFRedactionVerifier::COLOR_TOLERANCE &&
               std::abs(qGreen(pixel) - qGreen(color)) <= PDFRedactionVerifier::COLOR_TOLERANCE &&
               std::abs(qBlue(pixel) - qBlue(color)) <= PDFRedactionVerifier::COLOR_TOLERANCE;
    }

    /// The pixel of the rendered page is the paper (white) or the fill (6.8 step 2)
    static bool isAllowedColor(QRgb pixel, const std::optional<QColor>& fillColor)
    {
        return isNearColor(pixel, qRgb(255, 255, 255)) || (fillColor && isNearColor(pixel, fillColor->rgb()));
    }

    /// The pixel of an image is transparent or the fill - the paper (white) without a fill (B138,
    /// R6-5: PDFPrecompiledPage::redact gives an image no transparency)
    static bool isAllowedImagePixel(QRgb pixel, const std::optional<QColor>& fillColor)
    {
        return qAlpha(pixel) == 0 || (qAlpha(pixel) == 255 && isNearColor(pixel, fillColor ? fillColor->rgb() : qRgb(255, 255, 255)));
    }

    /// The mask of the area in the pixels of the transform (pixels, whose centers lie inside), in
    /// the rectangle \p bounds of the pixels
    static QImage createMask(const QPainterPath& area, const QTransform& toPixels, const QRect& bounds)
    {
        QImage mask(bounds.size(), QImage::Format_Alpha8);
        mask.fill(0);
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.translate(-bounds.topLeft());
        painter.setTransform(toPixels, true);
        painter.fillPath(area, QColor(0, 0, 0, 255));
        painter.end();
        return mask;
    }

    /// The pixel bounds of the area in the pixels of the transform within \p limits
    static QRect getPixelBounds(const QRectF& box, const QTransform& toPixels, const QRect& limits)
    {
        const QRectF mapped = toPixels.mapRect(box);
        if (!std::isfinite(mapped.left()) || !std::isfinite(mapped.top()) || !std::isfinite(mapped.right()) || !std::isfinite(mapped.bottom()))
        {
            return QRect();
        }
        return mapped.adjusted(-1.0, -1.0, 1.0, 1.0).toAlignedRect().intersected(limits);
    }

    /// The words of the characters of the layout, whose boxes meet the area (6.8 step 3: the
    /// invisible and the covered text too)
    static void collectWords(const PDFTextLayout& layout, const QPainterPath& area, std::set<QString>* words)
    {
        for (const PDFTextBlock& block : layout.getTextBlocks())
        {
            for (const PDFTextLine& line : block.getLines())
            {
                QString word;
                bool isUnderArea = false;
                auto finishWord = [&word, &isUnderArea, words]()
                {
                    if (isUnderArea && word.size() >= MINIMUM_WORD_LENGTH)
                    {
                        words->insert(word.toCaseFolded());
                    }
                    word.clear();
                    isUnderArea = false;
                };

                for (const TextCharacter& character : line.getCharacters())
                {
                    if (!character.character.isLetterOrNumber())
                    {
                        finishWord();
                        continue;
                    }

                    word += character.character;
                    const QRectF box = character.boundingBox.boundingRect();
                    if (box.isValid() ? area.intersects(box) : area.contains(character.position))
                    {
                        isUnderArea = true;
                    }
                }
                finishWord();
            }
        }
    }

    /// The page numbers of a warning ("3, 7, 12"), at most MAXIMUM_LISTED_PAGES of them
    static QString formatPages(const std::vector<PDFInteger>& pageIndices)
    {
        QStringList numbers;
        for (size_t i = 0; i < pageIndices.size() && i < MAXIMUM_LISTED_PAGES; ++i)
        {
            numbers << QString::number(pageIndices[i] + 1);
        }
        if (pageIndices.size() > MAXIMUM_LISTED_PAGES)
        {
            numbers << QStringLiteral("…");
        }
        return numbers.join(QStringLiteral(", "));
    }

    /// The commands of the content stream, which show text, or an inline image (the copy has
    /// neither: its text is curves, QPdfWriter writes images as XObjects); false, when the stream
    /// cannot be read
    static bool hasTextCommands(const QByteArray& content, bool* hasText)
    {
        *hasText = false;
        try
        {
            PDFLexicalAnalyzer analyzer(content.constData(), content.constData() + content.size());
            while (!analyzer.isAtEnd())
            {
                const PDFLexicalAnalyzer::Token token = analyzer.fetch();
                if (token.type == PDFLexicalAnalyzer::TokenType::EndOfFile)
                {
                    break;
                }
                if (token.type == PDFLexicalAnalyzer::TokenType::Command)
                {
                    const QByteArray command = token.data.toByteArray();
                    if (command == "Tj" || command == "TJ" || command == "'" || command == "\"" || command == "BI")
                    {
                        *hasText = true;
                        return true;
                    }
                }
            }
        }
        catch (const PDFException&)
        {
            return false;
        }
        return true;
    }

    /// Step 1: the structure of the copy (one revision, no encryption, the catalog, the pages and
    /// /Info of the allowed entries, every object reachable and of an allowed kind, the hidden data)
    static void checkStructure(const PDFDocument* output, const PDFRedactionVerifier::Options& options, std::vector<PDFRedactionVerifier::Finding>* errors);

    /// Step 3: the words of the areas in the texts of the copy
    static void checkWords(const PDFDocument* output, const std::set<QString>& words, const PDFRedactionVerifier::Options& options,
                           std::vector<PDFRedactionVerifier::Finding>* errors, std::vector<PDFRedactionVerifier::Finding>* warnings);

    /// Step 2 of one page of the copy: the areas, which are not clean; \p isUnreadable - a part of the
    /// page could not be read (its objects were not all checked)
    static std::vector<bool> checkPage(const PDFDocument* unfilledOutput, PDFInteger pageIndex, const std::vector<Component>& components,
                                       const std::optional<QColor>& fillColor, const PDFFontCache* fontCache, const PDFICCProfileCache* iccProfileCache,
                                       const PDFOptionalContentActivity* activity, bool* hasText, bool* isUnreadable);

    /// The page rendered on white paper at the resolution of the check (at most MAXIMUM_PIXEL_COUNT
    /// pixels); \p toPixels the matrix of the default user space to the pixels, \p scale its scale
    static QImage renderPage(const PDFDocument* document, const PDFPage* page, PDFInteger pageIndex, const PDFFontCache* fontCache,
                             const PDFICCProfileCache* iccProfileCache, const PDFOptionalContentActivity* activity, QTransform* toPixels, PDFReal* scale);

    /// Step 2, the fill (R6-5): the whole page of the copy -
    /// its fill included - shows exactly the fill color inside of each area shrunk by a pixel; the number
    /// of the areas, which it does not cover (a fill drawn elsewhere, in another color, or covered)
    static int checkFill(const PDFDocument* output, PDFInteger pageIndex, const std::vector<Component>& components, const QColor& fillColor,
                         const PDFFontCache* fontCache, const PDFICCProfileCache* iccProfileCache, const PDFOptionalContentActivity* activity);
};

/// The source page: its text (the words of the areas) and its soft masks (R6-16), with every layer
/// (6.8 step 3: IgnoreOptionalContent)
class PDFRedactionVerifierSourceProcessor : public PDFTextLayoutGenerator
{
    using BaseClass = PDFTextLayoutGenerator;

public:
    using BaseClass::BaseClass;

    bool hasSoftMask() const { return m_hasSoftMask; }

protected:
    virtual void performUpdateGraphicsState(const PDFPageContentProcessorState& state) override
    {
        if (state.getStateFlags().testFlag(PDFPageContentProcessorState::StateSoftMask) && state.getSoftMask())
        {
            m_hasSoftMask = true;
        }
        BaseClass::performUpdateGraphicsState(state);
    }

private:
    bool m_hasSoftMask = false;
};

/// The objects of a page of the copy without its fill (R6-5): a path, an outline of a stroke and a
/// mesh must not cover the inside of an area, a pixel of an image there is the fill or transparent;
/// a character of text is never drawn
class PDFRedactionVerifierPageChecker : public PDFPageContentProcessor
{
    using BaseClass = PDFPageContentProcessor;

public:
    explicit PDFRedactionVerifierPageChecker(const PDFPage* page, const PDFDocument* document, const PDFFontCache* fontCache, const PDFICCProfileCache* iccProfileCache,
                                             const PDFCMS* cms, const PDFOptionalContentActivity* activity,
                                             const std::vector<PDFRedactionVerifierHelper::Component>* components, std::optional<QColor> fillColor) :
        BaseClass(page, document, fontCache, iccProfileCache, cms, activity, QTransform(), PDFMeshQualitySettings()),
        m_components(components),
        m_fillColor(fillColor),
        m_failed(components->size(), false)
    {

    }

    const std::vector<bool>& getFailed() const { return m_failed; }
    bool hasText() const { return m_hasText; }

protected:
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override
    {
        Q_UNUSED(fillRule);
        if (text)
        {
            m_hasText = true;
        }

        const QTransform matrix = getCurrentWorldMatrix();
        if (fill)
        {
            checkArea(matrix.map(path));
        }
        if (stroke)
        {
            QPen pen = PDFPainterHelper::createPenFromState(getGraphicState(), 1.0);
            if (!(pen.widthF() > 0.0))
            {
                pen.setWidthF(0.01);
            }
            QPainterPathStroker stroker(pen);
            QPainterPath outline = stroker.createStroke(path);
            outline.setFillRule(Qt::WindingFill);
            checkArea(matrix.map(outline));
        }
    }

    virtual void performImagePainting(const QImage& image) override
    {
        if (image.isNull())
        {
            return;
        }

        // The pixel space of the image to the page, as PDFPainter draws it
        QTransform pixelToPage = QTransform(1.0 / image.width(), 0, 0, 1.0 / image.height(), 0, 0) * getCurrentWorldMatrix();
        pixelToPage.translate(0, image.height());
        pixelToPage.scale(1, -1);
        if (!pixelToPage.isInvertible())
        {
            return;
        }

        const QTransform pageToPixel = pixelToPage.inverted();
        const QRectF imageBox = pixelToPage.mapRect(QRectF(0, 0, image.width(), image.height()));
        const QImage argbImage = image.convertToFormat(QImage::Format_ARGB32);
        for (size_t i = 0; i < m_components->size(); ++i)
        {
            const PDFRedactionVerifierHelper::Component& component = (*m_components)[i];
            if (m_failed[i] || component.shrunk.isEmpty() || !component.shrunkBox.intersects(imageBox))
            {
                continue;
            }

            const QRect bounds = PDFRedactionVerifierHelper::getPixelBounds(component.shrunkBox, pageToPixel, argbImage.rect());
            if (bounds.isEmpty())
            {
                continue;
            }

            const QImage mask = PDFRedactionVerifierHelper::createMask(component.shrunk, pageToPixel, bounds);
            for (int y = 0; y < bounds.height() && !m_failed[i]; ++y)
            {
                const uchar* maskLine = mask.constScanLine(y);
                const QRgb* imageLine = reinterpret_cast<const QRgb*>(argbImage.constScanLine(bounds.top() + y));
                for (int x = 0; x < bounds.width(); ++x)
                {
                    if (!maskLine[x])
                    {
                        continue;
                    }

                    if (!PDFRedactionVerifierHelper::isAllowedImagePixel(imageLine[bounds.left() + x], m_fillColor))
                    {
                        m_failed[i] = true;
                        break;
                    }
                }
            }
        }
    }

    virtual void performMeshPainting(const PDFMesh& mesh) override
    {
        // The mesh is painted alone on a transparent image of each area: any pixel it covers inside
        // of the shrunk area is content, which was not removed (whatever its color)
        const PDFReal scale = PDFRedactionVerifier::CHECK_DPI / 72.0;
        for (size_t i = 0; i < m_components->size(); ++i)
        {
            const PDFRedactionVerifierHelper::Component& component = (*m_components)[i];
            if (m_failed[i] || component.shrunk.isEmpty())
            {
                continue;
            }

            const QTransform toPixels(scale, 0.0, 0.0, -scale, -component.shrunkBox.left() * scale, component.shrunkBox.bottom() * scale);
            const QRect limits(0, 0, qMax(1, qCeil(component.shrunkBox.width() * scale)), qMax(1, qCeil(component.shrunkBox.height() * scale)));
            if (qint64(limits.width()) * qint64(limits.height()) > PDFRedactionVerifier::MAXIMUM_PIXEL_COUNT)
            {
                // The area is checked by the rendering of the page only
                continue;
            }

            QImage coverage(limits.size(), QImage::Format_ARGB32_Premultiplied);
            coverage.fill(Qt::transparent);
            {
                QPainter painter(&coverage);
                painter.setTransform(toPixels);
                mesh.paint(&painter, 1.0);
            }

            const QImage mask = PDFRedactionVerifierHelper::createMask(component.shrunk, toPixels, limits);
            for (int y = 0; y < limits.height() && !m_failed[i]; ++y)
            {
                const uchar* maskLine = mask.constScanLine(y);
                const QRgb* coverageLine = reinterpret_cast<const QRgb*>(coverage.constScanLine(y));
                for (int x = 0; x < limits.width(); ++x)
                {
                    if (maskLine[x] && qAlpha(coverageLine[x]) != 0)
                    {
                        m_failed[i] = true;
                        break;
                    }
                }
            }
        }
    }

    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override
    {
        Q_UNUSED(info);
        m_hasText = true;
    }

    virtual void performClipping(const QPainterPath& path, Qt::FillRule fillRule) override
    {
        // The clip of the graphic state in the page space (8.5.4): what a path paints is the path
        // within the clip (the triangles of a mesh across the boundary of an area are clipped)
        QPainterPath clipPath = getCurrentWorldMatrix().map(path);
        clipPath.setFillRule(fillRule);
        m_clipPath = m_clipPath ? PDFPathBoolean::intersect(*m_clipPath, clipPath) : clipPath;
    }

    virtual void performSaveGraphicState(ProcessOrder order) override
    {
        if (order == ProcessOrder::AfterOperation)
        {
            m_clipStack.push_back(m_clipPath);
        }
    }

    virtual void performRestoreGraphicState(ProcessOrder order) override
    {
        if (order == ProcessOrder::BeforeOperation && !m_clipStack.empty())
        {
            m_clipPath = std::move(m_clipStack.back());
            m_clipStack.pop_back();
        }
    }

private:
    /// A filled area of the page within the clip must not cover the inside of an area
    void checkArea(const QPainterPath& pagePath)
    {
        const QRectF box = pagePath.controlPointRect();
        for (size_t i = 0; i < m_components->size(); ++i)
        {
            const PDFRedactionVerifierHelper::Component& component = (*m_components)[i];
            if (m_failed[i] || component.shrunk.isEmpty() || !component.shrunkBox.intersects(box))
            {
                continue;
            }

            QPainterPath covered = PDFPathBoolean::intersect(pagePath, component.shrunk);
            if (!covered.isEmpty() && m_clipPath)
            {
                covered = PDFPathBoolean::intersect(covered, *m_clipPath);
            }
            if (!covered.isEmpty())
            {
                m_failed[i] = true;
            }
        }
    }

    const std::vector<PDFRedactionVerifierHelper::Component>* m_components;
    std::optional<QColor> m_fillColor;
    std::vector<bool> m_failed;
    std::optional<QPainterPath> m_clipPath;                 ///< The clip in the page space, std::nullopt = none
    std::vector<std::optional<QPainterPath>> m_clipStack;   ///< The clips of the saved graphic states
    bool m_hasText = false;
};

void PDFRedactionVerifierHelper::checkStructure(const PDFDocument* output, const PDFRedactionVerifier::Options& options, std::vector<PDFRedactionVerifier::Finding>* errors)
{
    auto addError = [errors](const QString& message)
    {
        errors->push_back({ -1, message });
    };

    const PDFObjectStorage& storage = output->getStorage();
    const PDFDictionary* trailer = output->getTrailerDictionary();
    if (!trailer)
    {
        addError(tr("The redacted copy cannot be read."));
        return;
    }

    // One revision, no encryption (4.4: a whole new document)
    if (trailer->hasKey("Prev") || trailer->hasKey("XRefStm"))
    {
        addError(tr("The redacted copy has more than one revision."));
    }
    if (trailer->hasKey("Encrypt"))
    {
        addError(tr("The redacted copy is encrypted."));
    }

    // The catalog: the pages and the outline only (6.8 step 1)
    static constexpr std::array<const char*, 6> CATALOG_KEYS = { "Type", "Pages", "Outlines", "PageMode", "PageLayout", "Version" };
    const PDFDictionary* catalog = output->getDictionaryFromObject(trailer->get("Root"));
    if (!catalog)
    {
        addError(tr("The redacted copy has no catalog."));
        return;
    }
    for (size_t i = 0; i < catalog->getCount(); ++i)
    {
        const QByteArray key = catalog->getKey(i).getString();
        const bool isAllowed = std::any_of(CATALOG_KEYS.cbegin(), CATALOG_KEYS.cend(), [&key](const char* allowed) { return key == allowed; });
        if (!isAllowed || (key == "Outlines" && !options.isOutlineKept))
        {
            addError(tr("The catalog of the redacted copy has the entry /%1.").arg(QString::fromLatin1(key)));
        }
    }

    // The pages: the boxes, the rotation, the unit, the content and the resources only
    static constexpr std::array<const char*, 11> PAGE_KEYS = { "Type", "Parent", "MediaBox", "CropBox", "BleedBox", "TrimBox", "ArtBox", "Rotate", "UserUnit", "Contents", "Resources" };
    static constexpr std::array<const char*, 7> RESOURCE_KEYS = { "ExtGState", "XObject", "Pattern", "Shading", "ColorSpace", "ProcSet", "Font" };
    const PDFCatalog* pdfCatalog = output->getCatalog();
    for (size_t pageIndex = 0; pageIndex < pdfCatalog->getPageCount(); ++pageIndex)
    {
        const PDFPage* page = pdfCatalog->getPage(pageIndex);
        const PDFDictionary* pageDictionary = page ? output->getDictionaryFromObject(output->getObjectByReference(page->getPageReference())) : nullptr;
        if (!pageDictionary)
        {
            continue;
        }

        for (size_t i = 0; i < pageDictionary->getCount(); ++i)
        {
            const QByteArray key = pageDictionary->getKey(i).getString();
            if (std::none_of(PAGE_KEYS.cbegin(), PAGE_KEYS.cend(), [&key](const char* allowed) { return key == allowed; }))
            {
                errors->push_back({ PDFInteger(pageIndex), tr("Page %1 of the redacted copy has the entry /%2.").arg(pageIndex + 1).arg(QString::fromLatin1(key)) });
            }
        }

        if (const PDFDictionary* resources = output->getDictionaryFromObject(pageDictionary->get("Resources")))
        {
            for (size_t i = 0; i < resources->getCount(); ++i)
            {
                const QByteArray key = resources->getKey(i).getString();
                const bool isAllowed = std::any_of(RESOURCE_KEYS.cbegin(), RESOURCE_KEYS.cend(), [&key](const char* allowed) { return key == allowed; });
                const PDFDictionary* fonts = key == "Font" ? output->getDictionaryFromObject(resources->getValue(i)) : nullptr;
                if (!isAllowed || (key == "Font" && fonts && fonts->getCount() > 0))
                {
                    errors->push_back({ PDFInteger(pageIndex), tr("The resources of page %1 of the redacted copy have the entry /%2.").arg(pageIndex + 1).arg(QString::fromLatin1(key)) });
                }
            }
        }
    }

    // /Info: the standard keys of direct values only (R6-6)
    static constexpr std::array<const char*, 9> INFO_KEYS = { "Title", "Author", "Subject", "Keywords", "Creator", "Producer", "CreationDate", "ModDate", "Trapped" };
    if (const PDFDictionary* info = output->getDictionaryFromObject(trailer->get("Info")))
    {
        for (size_t i = 0; i < info->getCount(); ++i)
        {
            const QByteArray key = info->getKey(i).getString();
            const PDFObject& value = info->getValue(i);
            const bool isKnown = std::any_of(INFO_KEYS.cbegin(), INFO_KEYS.cend(), [&key](const char* allowed) { return key == allowed; });
            const bool isDirect = key == "Trapped" ? value.isName() : value.isString();
            if (!isKnown || !isDirect)
            {
                addError(tr("The document information of the redacted copy has the entry /%1.").arg(QString::fromLatin1(key)));
            }
        }
    }

    // Every object is reachable from the trailer and of an allowed kind (R6-14): one walk with one
    // visited set; no metadata stream, no embedded file, no annotation, no action other than GoTo
    std::set<PDFObjectReference> visited;
    std::vector<PDFObject> stack = { PDFObject::createDictionary(PDFDictionaryBuilder(*trailer)) };
    while (!stack.empty())
    {
        const PDFObject object = std::move(stack.back());
        stack.pop_back();

        if (object.isReference())
        {
            if (!visited.insert(object.getReference()).second)
            {
                continue;
            }
            stack.push_back(storage.getObjectByReference(object.getReference()));
            continue;
        }

        const PDFDictionary* dictionary = object.isDictionary() ? object.getDictionary() : (object.isStream() ? object.getStream()->getDictionary() : nullptr);
        if (dictionary)
        {
            const PDFObject& type = dictionary->get("Type");
            const PDFObject& subtype = dictionary->get("Subtype");
            const PDFObject& actionType = dictionary->get("S");
            const bool isForbiddenType = type.isName() && (type.getString() == "Metadata" || type.getString() == "EmbeddedFile" || type.getString() == "Annot" || type.getString() == "Filespec");
            const bool isForbiddenSubtype = subtype.isName() && subtype.getString() == "XML";
            const bool isForbiddenAction = actionType.isName() && actionType.getString() != "GoTo";
            if (isForbiddenType || isForbiddenSubtype || isForbiddenAction)
            {
                addError(tr("The redacted copy has an object, which was to be removed."));
            }

            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                stack.push_back(dictionary->getValue(i));
            }
        }
        else if (object.isArray())
        {
            for (const PDFObject& item : *object.getArray())
            {
                stack.push_back(item);
            }
        }
    }

    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    for (size_t i = 1; i < objects.size(); ++i)
    {
        if (!objects[i].object.isNull() && !visited.count(PDFObjectReference(PDFInteger(i), objects[i].generation)))
        {
            addError(tr("The redacted copy has an unused object."));
            break;
        }
    }

    // The hidden data (R6-14): what was kept by the options is not a finding. The invisible text is not
    // scanned here: verify checks every content stream of every page and every form XObject of the copy
    // for any text operator without a limit (an invisible text is a text) and the page keys exclude
    // /PieceInfo of an OCR layer; the scan stops at its budget of decoded content and then reports the
    // invisible text as present, so a large copy without any text was refused (BIBLE21.pdf, step 6)
    const PDFDocumentHiddenDataScan::Result scan = PDFDocumentHiddenDataScan::scan(output, false, nullptr, nullptr);
    for (const PDFDocumentHiddenDataScan::Kind kind : PDFDocumentHiddenDataScan::getKinds())
    {
        const bool isKept = kind == PDFDocumentHiddenDataScan::Kind::DocumentInfo || (kind == PDFDocumentHiddenDataScan::Kind::Outline && options.isOutlineKept);
        if (!isKept && scan.get(kind).isPresent)
        {
            addError(tr("The redacted copy has hidden data (%1).").arg(PDFDocumentHiddenDataScan::getName(kind)));
        }
    }
}

void PDFRedactionVerifierHelper::checkWords(const PDFDocument* output, const std::set<QString>& words, const PDFRedactionVerifier::Options& options,
                                            std::vector<PDFRedactionVerifier::Finding>* errors, std::vector<PDFRedactionVerifier::Finding>* warnings)
{
    if (words.empty())
    {
        return;
    }

    // Where a string lies: in /Info (its title separately), in the outline, elsewhere
    enum class Place
    {
        Title,
        Info,
        Outline,
        Other
    };

    const PDFObjectStorage& storage = output->getStorage();
    std::map<PDFObjectReference, Place> places;

    const PDFDictionary* trailer = output->getTrailerDictionary();
    const PDFObject& infoObject = trailer ? trailer->get("Info") : PDFObject();
    if (infoObject.isReference())
    {
        places[infoObject.getReference()] = Place::Info;
    }

    // The outline items: one walk with one visited set
    const PDFDictionary* catalog = trailer ? output->getDictionaryFromObject(trailer->get("Root")) : nullptr;
    std::vector<PDFObject> outlineStack;
    if (catalog)
    {
        outlineStack.push_back(catalog->get("Outlines"));
    }
    std::set<PDFObjectReference> visitedOutline;
    while (!outlineStack.empty())
    {
        const PDFObject object = outlineStack.back();
        outlineStack.pop_back();
        if (!object.isReference() || !visitedOutline.insert(object.getReference()).second)
        {
            continue;
        }
        places[object.getReference()] = Place::Outline;
        if (const PDFDictionary* item = storage.getDictionaryFromObject(storage.getObjectByReference(object.getReference())))
        {
            outlineStack.push_back(item->get("First"));
            outlineStack.push_back(item->get("Next"));
        }
    }

    std::set<Place> foundPlaces;
    auto checkString = [&words, &foundPlaces](const QByteArray& string, Place place)
    {
        const QString text = PDFEncoding::convertTextString(string).toCaseFolded();
        for (const QString& word : words)
        {
            if (text.contains(word))
            {
                foundPlaces.insert(place);
                return;
            }
        }
    };

    // The objects of the storage and a direct /Info of the trailer
    std::vector<std::pair<PDFObject, Place>> roots;
    if (infoObject.isDictionary())
    {
        roots.emplace_back(infoObject, Place::Info);
    }
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    for (size_t i = 1; i < objects.size(); ++i)
    {
        const PDFObjectReference reference(PDFInteger(i), objects[i].generation);
        auto placeIt = places.find(reference);
        roots.emplace_back(objects[i].object, placeIt != places.cend() ? placeIt->second : Place::Other);
    }

    for (const auto& [rootObject, rootPlace] : roots)
    {
        // The strings of the object and of its direct dictionaries and arrays (never the data of a stream)
        std::vector<std::pair<PDFObject, Place>> stack = { { rootObject, rootPlace } };
        while (!stack.empty())
        {
            const auto [object, place] = stack.back();
            stack.pop_back();

            if (object.isString())
            {
                checkString(object.getString(), place);
            }
            else if (object.isDictionary() || object.isStream())
            {
                const PDFDictionary* dictionary = object.isDictionary() ? object.getDictionary() : object.getStream()->getDictionary();
                for (size_t j = 0; j < dictionary->getCount(); ++j)
                {
                    const bool isTitle = place == Place::Info && dictionary->getKey(j).getString() == "Title";
                    stack.emplace_back(dictionary->getValue(j), isTitle ? Place::Title : place);
                }
            }
            else if (object.isArray())
            {
                for (const PDFObject& item : *object.getArray())
                {
                    stack.emplace_back(item, place);
                }
            }
        }
    }

    // Owner answer Q6.10: a redacted word in what the user has chosen to keep is a warning; anywhere
    // else in the copy it is an error
    if (foundPlaces.count(Place::Title))
    {
        (options.isTitleKept ? warnings : errors)->push_back({ -1, tr("The kept title contains a redacted word.") });
    }
    if (foundPlaces.count(Place::Info))
    {
        (options.isMetadataKept ? warnings : errors)->push_back({ -1, tr("The kept metadata contain a redacted word.") });
    }
    if (foundPlaces.count(Place::Outline))
    {
        (options.isOutlineKept ? warnings : errors)->push_back({ -1, tr("The kept outline contains a redacted word.") });
    }
    if (foundPlaces.count(Place::Other))
    {
        errors->push_back({ -1, tr("A text of the redacted copy contains a redacted word.") });
    }
}

std::vector<bool> PDFRedactionVerifierHelper::checkPage(const PDFDocument* unfilledOutput, PDFInteger pageIndex, const std::vector<Component>& components,
                                                        const std::optional<QColor>& fillColor, const PDFFontCache* fontCache, const PDFICCProfileCache* iccProfileCache,
                                                        const PDFOptionalContentActivity* activity, bool* hasText, bool* isUnreadable)
{
    std::vector<bool> failed(components.size(), false);
    *hasText = false;
    *isUnreadable = false;

    const PDFPage* page = unfilledOutput->getCatalog()->getPage(size_t(pageIndex));
    if (!page || components.empty())
    {
        return failed;
    }

    PDFCMSGeneric cms;

    // The objects of the page (R6-5): the geometry, the pixels of the images
    PDFRedactionVerifierPageChecker checker(page, unfilledOutput, fontCache, iccProfileCache, &cms, activity, &components, fillColor);
    const QList<PDFRenderError> errors = checker.processContents();
    *hasText = checker.hasText();
    for (const PDFRenderError& error : errors)
    {
        if (error.type == RenderErrorType::Error || error.type == RenderErrorType::NotImplemented)
        {
            // A page, which cannot be read whole (an object not decoded is not checked), cannot be
            // shown to be clean (owner answer Q6.9: never a copy unchecked)
            *isUnreadable = true;
            return failed;
        }
    }
    failed = checker.getFailed();

    // The pixels of the page without the fill (6.8 step 2): the inside of an area is the paper or
    // the fill (an image filled by B138), at 150 DPI, at most MAXIMUM_PIXEL_COUNT pixels
    QTransform toPixels;
    PDFReal scale = 1.0;
    const QImage rendered = renderPage(unfilledOutput, page, pageIndex, fontCache, iccProfileCache, activity, &toPixels, &scale);

    // The strip of one pixel of the check at the boundary is skipped: the area is shrunk by the
    // pixel of 150 DPI, a smaller scale of a huge page skips a wider strip
    const PDFReal pixelSize = 1.0 / scale;
    for (size_t i = 0; i < components.size(); ++i)
    {
        if (failed[i])
        {
            continue;
        }

        QPainterPath shrunk = components[i].shrunk;
        if (pixelSize > getPixelSize() + 1e-9)
        {
            QPainterPathStroker stroker;
            stroker.setWidth(2.0 * pixelSize);
            stroker.setJoinStyle(Qt::MiterJoin);
            shrunk = PDFPathBoolean::subtract(components[i].area, stroker.createStroke(components[i].area));
        }
        if (shrunk.isEmpty())
        {
            continue;
        }

        const QRect bounds = getPixelBounds(shrunk.boundingRect(), toPixels, rendered.rect());
        if (bounds.isEmpty())
        {
            continue;
        }

        const QImage mask = createMask(shrunk, toPixels, bounds);
        for (int y = 0; y < bounds.height() && !failed[i]; ++y)
        {
            const uchar* maskLine = mask.constScanLine(y);
            const QRgb* imageLine = reinterpret_cast<const QRgb*>(rendered.constScanLine(bounds.top() + y));
            for (int x = 0; x < bounds.width(); ++x)
            {
                if (maskLine[x] && !isAllowedColor(imageLine[bounds.left() + x], fillColor))
                {
                    failed[i] = true;
                    break;
                }
            }
        }
    }

    return failed;
}

QImage PDFRedactionVerifierHelper::renderPage(const PDFDocument* document, const PDFPage* page, PDFInteger pageIndex, const PDFFontCache* fontCache,
                                              const PDFICCProfileCache* iccProfileCache, const PDFOptionalContentActivity* activity, QTransform* toPixels, PDFReal* scale)
{
    const QRectF box = page->getMediaBox();
    *scale = PDFRedactionVerifier::CHECK_DPI / 72.0;
    const PDFReal pixelCount = box.width() * box.height() * *scale * *scale;
    if (pixelCount > PDFRedactionVerifier::MAXIMUM_PIXEL_COUNT)
    {
        *scale *= std::sqrt(PDFReal(PDFRedactionVerifier::MAXIMUM_PIXEL_COUNT) / pixelCount);
    }
    const QSize imageSize(qMax(1, qFloor(box.width() * *scale)), qMax(1, qFloor(box.height() * *scale)));

    PDFCMSGeneric cms;
    PDFRenderer renderer(document, fontCache, iccProfileCache, &cms, activity, PDFRenderer::None, PDFMeshQualitySettings());
    PDFPrecompiledPage compiledPage;
    renderer.compile(&compiledPage, size_t(pageIndex));

    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    *toPixels = QTransform(*scale, 0.0, 0.0, -*scale, -box.left() * *scale, box.bottom() * *scale);
    {
        QPainter painter(&image);
        compiledPage.draw(&painter, QRectF(), *toPixels, PDFRenderer::None, 1.0);
    }
    return image.convertToFormat(QImage::Format_ARGB32);
}

int PDFRedactionVerifierHelper::checkFill(const PDFDocument* output, PDFInteger pageIndex, const std::vector<Component>& components, const QColor& fillColor,
                                          const PDFFontCache* fontCache, const PDFICCProfileCache* iccProfileCache, const PDFOptionalContentActivity* activity)
{
    const PDFPage* page = output->getCatalog()->getPage(size_t(pageIndex));
    if (!page || components.empty())
    {
        return 0;
    }

    QTransform toPixels;
    PDFReal scale = 1.0;
    const QImage rendered = renderPage(output, page, pageIndex, fontCache, iccProfileCache, activity, &toPixels, &scale);

    int failedCount = 0;
    const PDFReal pixelSize = 1.0 / scale;
    for (const Component& component : components)
    {
        QPainterPath shrunk = component.shrunk;
        if (pixelSize > getPixelSize() + 1e-9)
        {
            QPainterPathStroker stroker;
            stroker.setWidth(2.0 * pixelSize);
            stroker.setJoinStyle(Qt::MiterJoin);
            shrunk = PDFPathBoolean::subtract(component.area, stroker.createStroke(component.area));
        }

        const QRect bounds = shrunk.isEmpty() ? QRect() : getPixelBounds(shrunk.boundingRect(), toPixels, rendered.rect());
        if (bounds.isEmpty())
        {
            continue;
        }

        bool isCovered = true;
        const QImage mask = createMask(shrunk, toPixels, bounds);
        for (int y = 0; y < bounds.height() && isCovered; ++y)
        {
            const uchar* maskLine = mask.constScanLine(y);
            const QRgb* imageLine = reinterpret_cast<const QRgb*>(rendered.constScanLine(bounds.top() + y));
            for (int x = 0; x < bounds.width(); ++x)
            {
                if (maskLine[x] && !isNearColor(imageLine[bounds.left() + x], fillColor.rgb()))
                {
                    isCovered = false;
                    break;
                }
            }
        }

        if (!isCovered)
        {
            ++failedCount;
        }
    }
    return failedCount;
}

PDFRedactionVerifier::Result PDFRedactionVerifier::verify(const PDFDocument* source, const std::vector<Area>& areas, const QByteArray& outputData, const Options& options,
                                                          const PDFOperationControl* control, PDFProgressSink* progress)
{
    using Helper = PDFRedactionVerifierHelper;

    Result result;
    if (!source || outputData.isEmpty())
    {
        result.errors.push_back({ -1, tr("The redacted copy cannot be checked.") });
        return result;
    }

    // The written bytes read back (no password: the copy is not encrypted)
    auto refusePassword = [](bool* ok) -> QString
    {
        *ok = false;
        return QString();
    };
    PDFDocumentReader reader(nullptr, refusePassword, false, false);
    const PDFDocument output = reader.readFromBuffer(outputData);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        result.errors.push_back({ -1, tr("The redacted copy cannot be read.") });
        return result;
    }

    const size_t pageCount = source->getCatalog()->getPageCount();
    if (output.getCatalog()->getPageCount() != pageCount)
    {
        result.errors.push_back({ -1, tr("The redacted copy does not have the pages of the document.") });
        return result;
    }

    // Step 1: the structure
    Helper::checkStructure(&output, options, &result.errors);

    // The areas of each page
    std::vector<QPainterPath> pageAreas(pageCount);
    for (QPainterPath& pageArea : pageAreas)
    {
        pageArea.setFillRule(Qt::WindingFill);
    }
    for (const Area& area : areas)
    {
        if (area.pageIndex >= 0 && size_t(area.pageIndex) < pageCount)
        {
            pageAreas[size_t(area.pageIndex)].addPath(area.path);
        }
    }

    // R6-5: the copy without the fill - the fill of a page with an area is its last content stream
    PDFDocumentBuilder unfilledBuilder(&output);
    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        if (!options.fillColor || pageAreas[pageIndex].isEmpty())
        {
            continue;
        }

        const PDFPage* page = output.getCatalog()->getPage(pageIndex);
        const PDFDictionary* pageDictionary = page ? output.getDictionaryFromObject(output.getObjectByReference(page->getPageReference())) : nullptr;
        const PDFObject& contents = pageDictionary ? output.getObject(pageDictionary->get("Contents")) : PDFObject();
        if (!contents.isArray() || contents.getArray()->getCount() < 2)
        {
            result.errors.push_back({ PDFInteger(pageIndex), tr("The fill of page %1 of the redacted copy is not its last content.").arg(pageIndex + 1) });
            continue;
        }

        PDFArrayBuilder unfilledContents;
        for (size_t i = 0; i + 1 < contents.getArray()->getCount(); ++i)
        {
            unfilledContents.appendItem(contents.getArray()->getItem(i));
        }
        PDFObjectFactory pageUpdate;
        pageUpdate.beginDictionary();
        pageUpdate.beginDictionaryItem("Contents");
        pageUpdate << PDFObject::createArray(std::move(unfilledContents));
        pageUpdate.endDictionaryItem();
        pageUpdate.endDictionary();
        unfilledBuilder.mergeTo(page->getPageReference(), pageUpdate.takeObject());
    }
    const PDFDocument unfilledOutput = unfilledBuilder.build();

    // The content streams of the copy show no text (6.8 step 1)
    const PDFObjectStorage::PDFObjects& objects = output.getStorage().getObjects();
    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        const PDFPage* page = output.getCatalog()->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        std::vector<PDFObject> contents;
        const PDFObject& pageContents = output.getObject(page->getContents());
        if (pageContents.isArray())
        {
            contents.insert(contents.end(), pageContents.getArray()->begin(), pageContents.getArray()->end());
        }
        else
        {
            contents.push_back(pageContents);
        }

        for (const PDFObject& content : contents)
        {
            const PDFObject& stream = output.getObject(content);
            bool hasText = false;
            if (!stream.isStream() || !Helper::hasTextCommands(output.getDecodedStream(stream.getStream()), &hasText) || hasText)
            {
                result.errors.push_back({ PDFInteger(pageIndex), tr("Page %1 of the redacted copy has text.").arg(pageIndex + 1) });
                break;
            }
        }
    }
    for (size_t i = 1; i < objects.size(); ++i)
    {
        const PDFObject& object = objects[i].object;
        const PDFObject& subtype = object.isStream() ? object.getStream()->getDictionary()->get("Subtype") : PDFObject();
        bool hasText = false;
        if (subtype.isName() && subtype.getString() == "Form" && (!Helper::hasTextCommands(output.getDecodedStream(object.getStream()), &hasText) || hasText))
        {
            result.errors.push_back({ -1, tr("A form of the redacted copy has text.") });
            break;
        }
    }

    // The source (words, soft masks) and the pages of the copy in parallel
    PDFOptionalContentActivity sourceActivity(source, OCUsage::View, nullptr);
    PDFOptionalContentActivity outputActivity(&unfilledOutput, OCUsage::View, nullptr);
    PDFFontCache sourceFontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFFontCache outputFontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache sourceIccProfileCache;
    PDFICCProfileCache outputIccProfileCache;
    sourceFontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(source), &sourceActivity));
    sourceIccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(source), &sourceActivity));
    outputFontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&unfilledOutput), &outputActivity));
    outputIccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&unfilledOutput), &outputActivity));

    // The copy with its fill: what the reader of the copy sees
    PDFOptionalContentActivity filledActivity(&output, OCUsage::View, nullptr);
    PDFFontCache filledFontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache filledIccProfileCache;
    filledFontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&output), &filledActivity));
    filledIccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&output), &filledActivity));

    if (progress)
    {
        ProgressStartupInfo info;
        info.showDialog = false;
        info.text = tr("Checking the redacted copy");
        progress->start(pageCount, std::move(info));
    }

    Helper::SharedState state;
    auto checkPage = [&](PDFInteger pageIndex)
    {
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return;
        }

        const QPainterPath& area = pageAreas[size_t(pageIndex)];

        // The source: the words under the areas, the soft masks of the page (R6-16)
        std::set<QString> words;
        bool hasSoftMask = false;
        if (const PDFPage* sourcePage = source->getCatalog()->getPage(size_t(pageIndex)))
        {
            PDFCMSGeneric cms;
            PDFRedactionVerifierSourceProcessor processor(PDFRenderer::IgnoreOptionalContent, sourcePage, source, &sourceFontCache, &sourceIccProfileCache, &cms,
                                                          &sourceActivity, QTransform(), PDFMeshQualitySettings());
            processor.setOperationControl(control);
            processor.processContents();
            hasSoftMask = processor.hasSoftMask();
            if (!area.isEmpty())
            {
                Helper::collectWords(processor.createTextLayout(), area, &words);
            }
        }

        // The page of the copy
        std::vector<PDFRedactionVerifier::Finding> pageErrors;
        if (!area.isEmpty())
        {
            const std::vector<Helper::Component> components = Helper::createComponents(area);
            bool hasText = false;
            bool isUnreadable = false;
            const std::vector<bool> failed = Helper::checkPage(&unfilledOutput, pageIndex, components, options.fillColor, &outputFontCache, &outputIccProfileCache, &outputActivity, &hasText, &isUnreadable);
            const int failedCount = int(std::count(failed.cbegin(), failed.cend(), true));
            if (isUnreadable)
            {
                pageErrors.push_back({ pageIndex, tr("Page %1 of the redacted copy cannot be checked.").arg(pageIndex + 1) });
            }
            if (failedCount > 0)
            {
                pageErrors.push_back({ pageIndex, tr("Content under %Ln areas on page %1 could not be removed.", nullptr, failedCount).arg(pageIndex + 1) });
            }
            if (hasText)
            {
                pageErrors.push_back({ pageIndex, tr("Page %1 of the redacted copy has text.").arg(pageIndex + 1) });
            }

            const int uncoveredCount = options.fillColor ? Helper::checkFill(&output, pageIndex, components, *options.fillColor, &filledFontCache, &filledIccProfileCache, &filledActivity) : 0;
            if (uncoveredCount > 0)
            {
                pageErrors.push_back({ pageIndex, tr("The fill does not cover %Ln areas on page %1.", nullptr, uncoveredCount).arg(pageIndex + 1) });
            }
        }

        {
            QMutexLocker locker(&state.mutex);
            state.words.insert(words.cbegin(), words.cend());
            state.errors.insert(state.errors.end(), pageErrors.cbegin(), pageErrors.cend());
            if (hasSoftMask)
            {
                state.softMaskPages.push_back(pageIndex);
            }
        }

        if (progress)
        {
            progress->step();
        }
    };

    PDFIntegerRange<PDFInteger> pageRange(0, PDFInteger(pageCount));
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageRange.begin(), pageRange.end(), checkPage);

    if (progress)
    {
        progress->finish();
    }

    if (PDFOperationControl::isOperationCanceled(control))
    {
        result.isStopped = true;
        return result;
    }

    // Step 3: the words in the texts of the copy
    Helper::checkWords(&output, state.words, options, &result.errors, &result.warnings);

    std::sort(state.errors.begin(), state.errors.end(), [](const Finding& left, const Finding& right) { return left.pageIndex < right.pageIndex; });
    result.errors.insert(result.errors.end(), state.errors.cbegin(), state.errors.cend());

    // Step 4: R6-16, owner answer Q6.19 a - the soft masks are a warning with the pages
    if (!state.softMaskPages.empty())
    {
        std::sort(state.softMaskPages.begin(), state.softMaskPages.end());
        result.warnings.push_back({ state.softMaskPages.front(), tr("Content hidden by a soft mask may be visible in the copy on %Ln pages: %1.", nullptr, int(state.softMaskPages.size()))
                                                                     .arg(Helper::formatPages(state.softMaskPages)) });
    }

    return result;
}

}   // namespace pdf
