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

#include "pdfocrpagepreparer.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfdocument.h"
#include "pdfpage.h"
#include "pdfcatalog.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfpainter.h"
#include "pdfblpainter.h"
#include "pdfannotation.h"
#include "pdfoptionalcontent.h"
#include "pdfmeshqualitysettings.h"
#include "pdfpagecontentprocessor.h"
#include "pdfdocumentwriter.h"
#include "pdfnumbertreeloader.h"

#include <QPainter>
#include <QCryptographicHash>
#include <QtMath>

#include <set>
#include <cmath>
#include <array>
#include <algorithm>

namespace pdf
{

// -------------------------------------------------------------------------
// Content analyzer
// -------------------------------------------------------------------------

/// Analyzes the page content: counts visible/invisible characters, images,
/// paths and detects suspicious text (INPUT-02, INPUT-03).
class PDFOCRPageContentAnalyzer : public PDFPageContentProcessor
{
public:
    explicit PDFOCRPageContentAnalyzer(const PDFPage* page,
                                       const PDFDocument* document,
                                       const PDFFontCache* fontCache,
                                       const PDFCMS* cms,
                                       const PDFOptionalContentActivity* optionalContentActivity,
                                       const PDFMeshQualitySettings& meshQualitySettings) :
        PDFPageContentProcessor(page, document, fontCache, cms, optionalContentActivity, QTransform(), meshQualitySettings),
        m_cropBox(page->getCropBox())
    {

    }

    int visibleCharacterCount = 0;
    int invisibleCharacterCount = 0;
    int transparentCharacterCount = 0;
    int outsideCharacterCount = 0;
    int coveredCharacterCount = 0;
    int unmappedCharacterCount = 0;
    int imageCount = 0;
    int pathCount = 0;
    int whitespaceCharacterCount = 0;

protected:
    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override
    {
        if (info.character.isSpace())
        {
            ++whitespaceCharacterCount;
            return;
        }

        const PDFPageContentProcessorState* state = getGraphicState();
        const TextRenderingMode mode = state->getTextRenderingMode();

        const QPointF position = info.matrix.map(QPointF(0.0, 0.0));
        if (!m_cropBox.contains(position))
        {
            ++outsideCharacterCount;
            return;
        }

        if (mode == TextRenderingMode::Invisible || mode == TextRenderingMode::Clip)
        {
            ++invisibleCharacterCount;
            m_invisibleCharacterRects.push_back(getCharacterRect(info));
            return;
        }

        const bool fill = isTextRenderingModeFilled(mode);
        const bool stroke = isTextRenderingModeStroked(mode);
        const PDFReal alpha = fill ? state->getAlphaFilling() : (stroke ? state->getAlphaStroking() : 0.0);
        if (alpha <= 0.0)
        {
            ++transparentCharacterCount;
            return;
        }

        VisibleCharacter character;
        character.rect = getCharacterRect(info);
        m_visibleCharacters.push_back(character);
        ++visibleCharacterCount;
    }

public:
    /// Returns rectangles of the visible characters (canonical page space)
    std::vector<QRectF> getVisibleCharacterRects() const
    {
        std::vector<QRectF> rects;
        rects.reserve(m_visibleCharacters.size());
        for (const VisibleCharacter& character : m_visibleCharacters)
        {
            rects.push_back(character.rect);
        }
        return rects;
    }

    /// Returns rectangles of the invisible characters (canonical page space)
    const std::vector<QRectF>& getInvisibleCharacterRects() const { return m_invisibleCharacterRects; }

protected:
    /// Rectangle of the character in the canonical page space. A glyphless font
    /// (invisible OCR text) has no outline, the advance and the font size are used.
    static QRectF getCharacterRect(const PDFTextCharacterInfo& info)
    {
        QRectF boundingRect = info.outline.boundingRect();
        if (boundingRect.isEmpty())
        {
            boundingRect = QRectF(0.0, 0.0, info.advance, info.fontSize);
        }
        return info.matrix.mapRect(boundingRect);
    }

    virtual void performProcessTextSequence(const TextSequence& textSequence, ProcessOrder order) override
    {
        if (order != ProcessOrder::BeforeOperation)
        {
            return;
        }

        for (const TextSequenceItem& item : textSequence.items)
        {
            if (item.isCharacter() && item.character.isNull())
            {
                ++unmappedCharacterCount;
            }
        }
    }

    virtual bool performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference) override
    {
        Q_UNUSED(image);
        Q_UNUSED(stream);
        Q_UNUSED(reference);

        registerImage();

        // Image is handled, we do not need the conversion to QImage
        return true;
    }

    virtual void performImagePainting(const QImage& image) override
    {
        Q_UNUSED(image);
        registerImage();
    }

    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override
    {
        Q_UNUSED(fillRule);

        if (!text && (stroke || fill) && !path.isEmpty())
        {
            ++pathCount;
        }
    }

    virtual void performMeshPainting(const PDFMesh& mesh) override
    {
        Q_UNUSED(mesh);
        ++pathCount;
    }

    virtual bool isContentKindSuppressed(ContentKind kind) const override
    {
        Q_UNUSED(kind);
        return false;
    }

private:
    struct VisibleCharacter
    {
        QRectF rect;
        bool covered = false;
    };

    void registerImage()
    {
        ++imageCount;

        // Image occupies the unit square in the current transformation matrix
        const QTransform matrix = getGraphicState()->getCurrentTransformationMatrix();
        const QRectF imageRect = matrix.mapRect(QRectF(0.0, 0.0, 1.0, 1.0));

        for (VisibleCharacter& character : m_visibleCharacters)
        {
            if (!character.covered && imageRect.contains(character.rect))
            {
                character.covered = true;
                ++coveredCharacterCount;
            }
        }
    }

    QRectF m_cropBox;
    std::vector<VisibleCharacter> m_visibleCharacters;
    std::vector<QRectF> m_invisibleCharacterRects;
};

/// Merges the rectangles of the characters into the rectangles of the lines: characters
/// with an overlapping vertical extent and a small horizontal gap form one rectangle.
static std::vector<QRectF> clusterTextRectangles(std::vector<QRectF> characters)
{
    std::erase_if(characters, [](const QRectF& rect) { return !rect.isValid() || rect.isEmpty() || !std::isfinite(rect.left()) || !std::isfinite(rect.top()) || !std::isfinite(rect.width()) || !std::isfinite(rect.height()); });
    std::sort(characters.begin(), characters.end(), [](const QRectF& left, const QRectF& right) { return left.left() < right.left(); });

    std::vector<QRectF> lines;
    for (const QRectF& character : characters)
    {
        bool merged = false;
        for (QRectF& line : lines)
        {
            const double verticalOverlap = qMin(line.bottom(), character.bottom()) - qMax(line.top(), character.top());
            const double minimumHeight = qMin(line.height(), character.height());
            const double horizontalGap = qMax(line.left(), character.left()) - qMin(line.right(), character.right());
            if (verticalOverlap > 0.5 * minimumHeight && horizontalGap < 2.0 * character.height())
            {
                line = line.united(character);
                merged = true;
                break;
            }
        }

        if (!merged)
        {
            lines.push_back(character);
        }
    }

    return lines;
}

// -------------------------------------------------------------------------
// PDFOCRPagePreparer
// -------------------------------------------------------------------------

PDFOCRPagePreparer::PDFOCRPagePreparer(const PDFDocument* document,
                                       const PDFFontCache* fontCache,
                                       const PDFCMS* cms,
                                       const PDFOptionalContentActivity* optionalContentActivity,
                                       const PDFMeshQualitySettings& meshQualitySettings,
                                       RendererEngine rendererEngine) :
    m_document(document),
    m_fontCache(fontCache),
    m_cms(cms),
    m_optionalContentActivity(optionalContentActivity),
    m_meshQualitySettings(meshQualitySettings),
    m_rendererEngine(rendererEngine)
{

}

std::vector<const PDFOCRRegion*> PDFOCRPagePreparer::getRegionsCollidingWithText(const PDFOCRPageAnalysis& analysis, const std::vector<PDFOCRRegion>& regions)
{
    std::vector<const PDFOCRRegion*> colliding;
    for (const PDFOCRRegion& region : regions)
    {
        if (region.type == PDFOCRRegionType::Recognize && intersectsAny(region.rect, analysis.textRectangles))
        {
            colliding.push_back(&region);
        }
    }
    return colliding;
}

PDFOCRPagePreparer::PolicyDecision PDFOCRPagePreparer::evaluateExistingTextPolicy(const PDFOCRPageAnalysis& analysis,
                                                                                 PDFOCRExistingTextPolicy policy,
                                                                                 bool hasInclusiveRegions,
                                                                                 QString* reason,
                                                                                 const std::vector<PDFOCRRegion>* regions)
{
    auto decide = [reason](PolicyDecision decision, const QString& text)
    {
        if (reason)
        {
            *reason = text;
        }
        return decision;
    };

    // An inclusive region over the existing text is a collision (chapter 6.2): the
    // recognized words would be written over a digital or a foreign text
    if (regions && policy != PDFOCRExistingTextPolicy::ReviewOnly)
    {
        const std::vector<const PDFOCRRegion*> colliding = getRegionsCollidingWithText(analysis, *regions);
        if (!colliding.empty())
        {
            QStringList names;
            for (const PDFOCRRegion* region : colliding)
            {
                names << (region->name.isEmpty() ? QString::number(region->id) : region->name);
            }
            return decide(PolicyDecision::NeedsDecision, PDFTranslationContext::tr("Region(s) %1 overlap the existing text of the page. Move the regions, or recognize the page for the review only.").arg(names.join(QStringLiteral(", "))));
        }
    }

    switch (policy)
    {
        case PDFOCRExistingTextPolicy::ReviewOnly:
            return decide(PolicyDecision::Recognize, QString());

        case PDFOCRExistingTextPolicy::AddInRegions:
            if (hasInclusiveRegions)
            {
                return decide(PolicyDecision::Recognize, QString());
            }
            if (analysis.hasUsableVisibleText() || analysis.hasOwnOCRLayer)
            {
                return decide(PolicyDecision::NeedsDecision, PDFTranslationContext::tr("Page contains text. Define the regions to recognize."));
            }
            return decide(PolicyDecision::Recognize, QString());

        case PDFOCRExistingTextPolicy::ReplaceOwnLayer:
            if (analysis.hasOwnOCRLayer)
            {
                return decide(PolicyDecision::Recognize, QString());
            }
            if (analysis.hasUsableVisibleText())
            {
                return decide(PolicyDecision::Skip, PDFTranslationContext::tr("Page contains visible text, which is not an own OCR layer."));
            }
            if (analysis.contentClass == PDFOCRPageContentClass::InvisibleText)
            {
                return decide(PolicyDecision::Skip, PDFTranslationContext::tr("Page contains invisible text of foreign origin, which cannot be replaced automatically."));
            }
            return decide(PolicyDecision::Recognize, QString());

        case PDFOCRExistingTextPolicy::OnlyPagesWithoutText:
            break;
    }

    if (analysis.hasOwnOCRLayer)
    {
        return decide(PolicyDecision::Skip, PDFTranslationContext::tr("Page already has an own OCR layer. Use the mode replacing the own layer."));
    }

    switch (analysis.contentClass)
    {
        case PDFOCRPageContentClass::Image:
        case PDFOCRPageContentClass::Empty:
            return decide(PolicyDecision::Recognize, QString());

        case PDFOCRPageContentClass::VisibleText:
            return decide(PolicyDecision::Skip, PDFTranslationContext::tr("Page contains usable visible text (%n character(s)).", nullptr, analysis.visibleCharacterCount));

        case PDFOCRPageContentClass::InvisibleText:
            return decide(PolicyDecision::Skip, PDFTranslationContext::tr("Page contains invisible text of foreign origin (%n character(s)). Use the review-only mode or the regions.", nullptr, analysis.invisibleCharacterCount));

        case PDFOCRPageContentClass::Mixed:
            return decide(PolicyDecision::NeedsDecision, PDFTranslationContext::tr("Page contains both text and images. Decide manually or define the regions."));

        case PDFOCRPageContentClass::Ambiguous:
            return decide(PolicyDecision::NeedsDecision, analysis.ambiguityReasons.isEmpty() ? PDFTranslationContext::tr("Page content is ambiguous.") : analysis.ambiguityReasons.join(QChar(' ')));

        case PDFOCRPageContentClass::Unknown:
            break;
    }

    return decide(PolicyDecision::NeedsDecision, PDFTranslationContext::tr("Page was not analyzed."));
}

PDFOCRPageAnalysis PDFOCRPagePreparer::analyze(PDFInteger pageIndex, const PDFOperationControl* operationControl) const
{
    PDFOCRPageAnalysis analysis;

    const PDFCatalog* catalog = m_document->getCatalog();
    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        analysis.contentClass = PDFOCRPageContentClass::Unknown;
        analysis.ambiguityReasons << PDFTranslationContext::tr("Page does not exist.");
        return analysis;
    }

    const PDFPage* page = catalog->getPage(pageIndex);
    const PDFObjectStorage* storage = &m_document->getStorage();

    // Content analysis
    PDFOCRPageContentAnalyzer analyzer(page, m_document, m_fontCache, m_cms, m_optionalContentActivity, m_meshQualitySettings);
    analyzer.setOperationControl(operationControl);
    analyzer.processContents();

    analysis.visibleCharacterCount = analyzer.visibleCharacterCount;
    analysis.invisibleCharacterCount = analyzer.invisibleCharacterCount;
    analysis.imageCount = analyzer.imageCount;
    analysis.unmappedCharacterCount = analyzer.unmappedCharacterCount;
    analysis.hasImages = analyzer.imageCount > 0;
    analysis.hasInvisibleText = analyzer.invisibleCharacterCount > 0;

    // Annotations
    for (const PDFObjectReference& annotationReference : page->getAnnotations())
    {
        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            break;
        }

        PDFAnnotationPtr annotation = PDFAnnotation::parse(storage, annotationReference);
        if (!annotation)
        {
            continue;
        }

        const AnnotationType type = annotation->getType();
        if (type == AnnotationType::Popup || type == AnnotationType::Link)
        {
            continue;
        }

        const PDFAnnotation::Flags flags = annotation->getEffectiveFlags();
        const bool isHidden = flags.testFlag(PDFAnnotation::Hidden);

        analysis.hasAnnotations = true;

        if (type == AnnotationType::Widget)
        {
            analysis.hasFormFields = true;
        }

        if (type == AnnotationType::Redact)
        {
            analysis.hasUnappliedRedactions = true;
            analysis.redactionRectangles.push_back(annotation->getRectangle());
            continue;
        }

        if (isHidden)
        {
            continue;
        }

        // Only annotations with visible appearance cover the page content
        if (!annotation->getAppearanceStreams().getAppearance().isNull())
        {
            const QRectF rect = annotation->getRectangle();
            if (rect.isValid() && !rect.isEmpty())
            {
                analysis.annotationRectangles.push_back(rect);
            }
        }
    }

    // Own OCR layer. Only a layer bound to its content is the own layer; a layer
    // changed by another tool is a foreign invisible text (INPUT-05).
    std::vector<QRectF> ownLayerWordRects;
    PDFOCRTextLayerWriter::LayerInfo layerInfo;
    const std::optional<PDFOCRPageResult> ownLayer = PDFOCRTextLayerWriter::readLayer(m_document, pageIndex, &layerInfo);
    if (layerInfo.isPresent)
    {
        analysis.hasOwnOCRLayer = true;
        analysis.ownLayerId = layerInfo.layerId;

        if (!layerInfo.fingerprintMatches)
        {
            analysis.notes << PDFTranslationContext::tr("Own OCR layer metadata do not match the current page content (page was modified by another tool).");
        }
        if (!layerInfo.isContentOwn)
        {
            analysis.notes << PDFTranslationContext::tr("The text layer of the own OCR was changed by another tool; it is treated as a foreign text.");
        }

        if (ownLayer)
        {
            for (const PDFOCRWord* word : ownLayer->getWords())
            {
                ownLayerWordRects.push_back(word->quad.boundingRect());
            }
        }
    }

    // Rectangles of the existing text (INPUT-04, chapter 6.2): visible digital text and
    // invisible text of foreign origin; the text of the own layer is not a collision.
    std::vector<QRectF> characterRects = analyzer.getVisibleCharacterRects();
    int foreignInvisibleCharacterCount = 0;
    for (const QRectF& rect : analyzer.getInvisibleCharacterRects())
    {
        bool isOwn = false;
        for (const QRectF& ownRect : ownLayerWordRects)
        {
            if (ownRect.adjusted(-0.5, -0.5, 0.5, 0.5).contains(rect.center()))
            {
                isOwn = true;
                break;
            }
        }
        if (!isOwn)
        {
            ++foreignInvisibleCharacterCount;
            characterRects.push_back(rect);
        }
    }
    analysis.textRectangles = clusterTextRectangles(std::move(characterRects));

    analysis.isTagged = isTaggedDocument(m_document);

    // Classification
    constexpr int UsableTextThreshold = 40;
    const bool usableVisibleText = analyzer.visibleCharacterCount - analyzer.coveredCharacterCount >= UsableTextThreshold;
    const bool hasContent = analyzer.visibleCharacterCount > 0 || analyzer.invisibleCharacterCount > 0 || analyzer.imageCount > 0 || analyzer.pathCount > 0;
    const bool hasAnyForeignText = analyzer.visibleCharacterCount > 0 || foreignInvisibleCharacterCount > 0;

    analysis.hasVisibleText = usableVisibleText;

    if (!hasContent)
    {
        analysis.contentClass = PDFOCRPageContentClass::Empty;
    }
    else if (usableVisibleText && analyzer.imageCount > 0)
    {
        analysis.contentClass = PDFOCRPageContentClass::Mixed;
    }
    else if (usableVisibleText)
    {
        analysis.contentClass = PDFOCRPageContentClass::VisibleText;
    }
    else if (foreignInvisibleCharacterCount >= UsableTextThreshold)
    {
        analysis.contentClass = PDFOCRPageContentClass::InvisibleText;
    }
    else if (analyzer.imageCount > 0 && hasAnyForeignText)
    {
        // A scan with a small digital text (page number, stamp) or a short foreign
        // invisible text is a mixed page: it must not be accepted automatically as
        // a page without text, the OCR could write over the existing text (INPUT-02)
        analysis.contentClass = PDFOCRPageContentClass::Mixed;
    }
    else if (analyzer.imageCount > 0)
    {
        analysis.contentClass = PDFOCRPageContentClass::Image;
    }
    else
    {
        analysis.contentClass = PDFOCRPageContentClass::Ambiguous;
        analysis.ambiguityReasons << PDFTranslationContext::tr("Page contains only vector graphics or a small amount of text.");
    }

    // Ambiguity reasons (INPUT-03)
    if (analyzer.unmappedCharacterCount > 0)
    {
        analysis.contentClass = PDFOCRPageContentClass::Ambiguous;
        analysis.ambiguityReasons << PDFTranslationContext::tr("%n character(s) without unicode mapping.", nullptr, analyzer.unmappedCharacterCount);
    }

    if (analyzer.coveredCharacterCount > 0)
    {
        analysis.contentClass = PDFOCRPageContentClass::Ambiguous;
        analysis.ambiguityReasons << PDFTranslationContext::tr("%n character(s) covered by an image.", nullptr, analyzer.coveredCharacterCount);
    }

    if (analyzer.transparentCharacterCount > 0)
    {
        analysis.ambiguityReasons << PDFTranslationContext::tr("%n character(s) with zero opacity.", nullptr, analyzer.transparentCharacterCount);
        if (!usableVisibleText)
        {
            analysis.contentClass = PDFOCRPageContentClass::Ambiguous;
        }
    }

    if (analyzer.outsideCharacterCount > 0)
    {
        analysis.ambiguityReasons << PDFTranslationContext::tr("%n character(s) outside of the visible area.", nullptr, analyzer.outsideCharacterCount);
    }

    if (analyzer.visibleCharacterCount > 0 && !usableVisibleText && analyzer.imageCount > 0)
    {
        analysis.notes << PDFTranslationContext::tr("Page contains a small amount of digital text (%n character(s)), for example a page number. Decide, whether the page is recognized with the existing text masked.", nullptr, analyzer.visibleCharacterCount);
    }

    if (foreignInvisibleCharacterCount > 0)
    {
        analysis.notes << PDFTranslationContext::tr("Page contains invisible text of foreign origin (%n character(s)).", nullptr, foreignInvisibleCharacterCount);
    }

    if (analysis.hasUnappliedRedactions)
    {
        analysis.notes << PDFTranslationContext::tr("Page contains unapplied redaction annotations. Affected areas are excluded from the recognition.");
    }

    return analysis;
}

bool PDFOCRPagePreparer::isTaggedDocument(const PDFDocument* document)
{
    const PDFCatalog* catalog = document->getCatalog();
    return !catalog->getStructureTreeRoot().isNull() || catalog->isLogicalStructureMarked();
}

bool PDFOCRPagePreparer::hasConformanceDeclaration(const PDFDocument* document, QStringList* declarations)
{
    const PDFCatalog* catalog = document->getCatalog();
    const PDFObject& metadataObject = document->getObject(catalog->getMetadata());
    if (!metadataObject.isStream())
    {
        return false;
    }

    // The properties are identified by their namespace, whatever prefix the producer used (PDF-15)
    const QByteArray metadata = document->getDecodedStream(metadataObject.getStream());
    return PDFOCRTextLayerWriter::findConformanceDeclarations(metadata, declarations, nullptr);
}

static QString toRoman(PDFInteger number, bool uppercase)
{
    if (number <= 0 || number >= 4000)
    {
        return QString::number(number);
    }

    static const std::array<std::pair<int, const char*>, 13> table =
    { {
        { 1000, "M" }, { 900, "CM" }, { 500, "D" }, { 400, "CD" }, { 100, "C" }, { 90, "XC" },
        { 50, "L" }, { 40, "XL" }, { 10, "X" }, { 9, "IX" }, { 5, "V" }, { 4, "IV" }, { 1, "I" }
    } };

    QString result;
    PDFInteger remaining = number;
    for (const auto& item : table)
    {
        while (remaining >= item.first)
        {
            result += QLatin1String(item.second);
            remaining -= item.first;
        }
    }

    return uppercase ? result : result.toLower();
}

static QString toLetters(PDFInteger number, bool uppercase)
{
    if (number <= 0)
    {
        return QString::number(number);
    }

    // 1 = A, 26 = Z, 27 = AA, ...
    const int count = int((number - 1) / 26) + 1;
    const QChar letter = QChar(int((uppercase ? 'A' : 'a') + (number - 1) % 26));
    return QString(count, letter);
}

QString PDFOCRPagePreparer::getPageLabel(const PDFDocument* document, PDFInteger pageIndex)
{
    const PDFDictionary* trailer = document->getTrailerDictionary();
    if (!trailer)
    {
        return QString();
    }

    const PDFDictionary* catalogDictionary = document->getDictionaryFromObject(trailer->get("Root"));
    if (!catalogDictionary || !catalogDictionary->hasKey("PageLabels"))
    {
        return QString();
    }

    const std::vector<PDFPageLabel> labels = PDFNumberTreeLoader<PDFPageLabel>::parse(&document->getStorage(), catalogDictionary->get("PageLabels"));
    const PDFPageLabel* label = nullptr;
    for (const PDFPageLabel& item : labels)
    {
        if (item.getPageIndex() <= pageIndex)
        {
            label = &item;
        }
        else
        {
            break;
        }
    }

    if (!label)
    {
        return QString();
    }

    const PDFInteger number = label->getPageStartNumber() + (pageIndex - label->getPageIndex());
    QString numberText;
    switch (label->getNumberingStyle())
    {
        case PDFPageLabel::NumberingStyle::None:
            break;
        case PDFPageLabel::NumberingStyle::DecimalArabic:
            numberText = QString::number(number);
            break;
        case PDFPageLabel::NumberingStyle::UppercaseRoman:
            numberText = toRoman(number, true);
            break;
        case PDFPageLabel::NumberingStyle::LowercaseRoman:
            numberText = toRoman(number, false);
            break;
        case PDFPageLabel::NumberingStyle::UppercaseLetters:
            numberText = toLetters(number, true);
            break;
        case PDFPageLabel::NumberingStyle::LowercaseLetters:
            numberText = toLetters(number, false);
            break;
    }

    return label->getPrefix() + numberText;
}

static const QByteArray EMPTY_DICTIONARY_DIGEST = QByteArrayLiteral("EMPTY-DICTIONARY");

/// Computes digest of the object. Dictionaries, which are empty after skipping
/// the keys of the own layer, have the same digest as a missing dictionary, so
/// adding/removing the own font resource does not change the fingerprint.
static QByteArray digestObject(const PDFObject& object,
                               const PDFObjectStorage* storage,
                               int depth,
                               std::set<PDFObjectReference>& visited,
                               const std::function<bool(const QByteArray&)>& skipKey)
{
    if (object.isReference())
    {
        // References are transparent: the digest must not depend on the fact, whether
        // a dictionary is direct or indirect, because the writer of the text layer
        // turns the indirect resource dictionaries into direct ones. Only the cycles
        // are detected (by the path from the root, so the result does not depend on
        // the order of the entries).
        const PDFObjectReference reference = object.getReference();
        if (visited.count(reference))
        {
            return QByteArrayLiteral("R");
        }
        visited.insert(reference);
        QByteArray digest = digestObject(storage->getObjectByReference(reference), storage, depth, visited, skipKey);
        visited.erase(reference);
        return digest;
    }

    if ((object.isStream() || object.isDictionary() || object.isArray()) && depth <= 0)
    {
        // Depth of the nesting is limited (protection against a pathological structure);
        // the object is digested in its serialized form, so different objects at the
        // limit have different digests (no common constant).
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArrayLiteral("X"));
        hash.addData(PDFDocumentWriter::getSerializedObject(object.isStream() ? PDFObject::createDictionary(std::make_shared<PDFDictionary>(*object.getStream()->getDictionary())) : object));
        if (object.isStream())
        {
            hash.addData(*object.getStream()->getContent());
        }
        return hash.result();
    }

    if (object.isStream())
    {
        // The whole (raw) content of the stream: a changed image or a changed appearance
        // must change the fingerprint (EXPORT-04, INPUT-05)
        const PDFStream* stream = object.getStream();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArrayLiteral("S"));
        hash.addData(digestObject(PDFObject::createDictionary(std::make_shared<PDFDictionary>(*stream->getDictionary())), storage, depth, visited, skipKey));
        const QByteArray* content = stream->getContent();
        hash.addData(QByteArray::number(content->size()));
        hash.addData(*content);
        return hash.result();
    }

    if (object.isDictionary())
    {
        const PDFDictionary* dictionary = object.getDictionary();
        std::vector<std::pair<QByteArray, QByteArray>> items;
        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            const QByteArray key = dictionary->getKey(i).getString();
            if (skipKey(key))
            {
                continue;
            }

            const QByteArray digest = digestObject(dictionary->getValue(i), storage, depth - 1, visited, skipKey);
            if (digest == EMPTY_DICTIONARY_DIGEST)
            {
                continue;
            }

            items.emplace_back(key, digest);
        }

        if (items.empty())
        {
            return EMPTY_DICTIONARY_DIGEST;
        }

        std::sort(items.begin(), items.end());
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArrayLiteral("D"));
        for (const auto& item : items)
        {
            hash.addData(item.first);
            hash.addData(item.second);
        }
        return hash.result();
    }

    if (object.isArray())
    {
        const PDFArray* array = object.getArray();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArrayLiteral("A"));
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            hash.addData(digestObject(array->getItem(i), storage, depth - 1, visited, skipKey));
        }
        return hash.result();
    }

    if (object.isNull())
    {
        // Missing value is the same as an empty dictionary (page without resources
        // gets an empty resource dictionary, when the own layer is removed)
        return EMPTY_DICTIONARY_DIGEST;
    }

    return PDFDocumentWriter::getSerializedObject(object);
}

QByteArray PDFOCRPagePreparer::computePageFingerprint(const PDFDocument* document, PDFInteger pageIndex)
{
    const PDFCatalog* catalog = document->getCatalog();
    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return QByteArray();
    }

    const PDFPage* page = catalog->getPage(pageIndex);
    const PDFObjectStorage* storage = &document->getStorage();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("PDF4QT-OCR-PAGE-FINGERPRINT-2"));

    auto addRect = [&hash](const QRectF& rect)
    {
        hash.addData(QByteArray::number(rect.left(), 'f', 3));
        hash.addData(QByteArray::number(rect.top(), 'f', 3));
        hash.addData(QByteArray::number(rect.width(), 'f', 3));
        hash.addData(QByteArray::number(rect.height(), 'f', 3));
    };

    addRect(page->getMediaBox());
    addRect(page->getCropBox());
    hash.addData(QByteArray::number(int(page->getPageRotation())));
    hash.addData(QByteArray::number(page->getUserUnit(), 'f', 4));

    // Own OCR layer (text layer and the isolation of the foreign content) is excluded from the fingerprint
    const std::vector<PDFObjectReference> ownContentReferences = PDFOCRTextLayerWriter::getOwnLayerContentReferences(document, pageIndex);
    auto isOwnContent = [&ownContentReferences](PDFObjectReference reference)
    {
        return std::find(ownContentReferences.begin(), ownContentReferences.end(), reference) != ownContentReferences.end();
    };

    // Content streams
    std::vector<const PDFStream*> contentStreams;
    for (const PDFObjectReference& contentReference : PDFOCRTextLayerWriter::getPageContentReferences(document, pageIndex))
    {
        if (isOwnContent(contentReference))
        {
            continue;
        }

        const PDFObject& contentObject = document->getObjectByReference(contentReference);
        if (contentObject.isStream())
        {
            contentStreams.push_back(contentObject.getStream());
        }
    }

    for (const PDFStream* stream : contentStreams)
    {
        const QByteArray data = document->getDecodedStream(stream);
        hash.addData(QByteArray::number(data.size()));
        hash.addData(data);
    }

    // Resources (own font is excluded). The back references to the page tree and to
    // the page are skipped, cycles are detected by the path from the root.
    std::set<PDFObjectReference> visited;
    auto skipKey = [](const QByteArray& key)
    {
        return key.startsWith(PDFOCRTextLayerWriter::FONT_RESOURCE_PREFIX) || key == "Parent" || key == "P";
    };
    constexpr int MaximumDepth = 64;
    hash.addData(QByteArrayLiteral("RES"));
    hash.addData(digestObject(page->getResources(), storage, MaximumDepth, visited, skipKey));

    // Annotations: their rectangles, flags and appearances cover the page content and
    // an unapplied redaction blocks the recognition (IMAGE-03, PDF-13)
    hash.addData(QByteArrayLiteral("ANNOTS"));
    for (const PDFObjectReference& annotationReference : page->getAnnotations())
    {
        hash.addData(digestObject(PDFObject::createReference(annotationReference), storage, MaximumDepth, visited, skipKey));
    }

    // Default configuration of the optional content (visibility of the layers, JOB-10)
    hash.addData(QByteArrayLiteral("OC"));
    if (const PDFDictionary* catalogDictionary = document->getDictionaryFromObject(document->getTrailerDictionary()->get("Root")))
    {
        if (const PDFDictionary* properties = document->getDictionaryFromObject(catalogDictionary->get("OCProperties")))
        {
            hash.addData(digestObject(properties->get("D"), storage, MaximumDepth, visited, skipKey));
        }
    }

    return hash.result();
}

QByteArray PDFOCRPagePreparer::computeDocumentFingerprint(const PDFDocument* document)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("PDF4QT-OCR-DOCUMENT-FINGERPRINT-1"));

    const size_t pageCount = document->getCatalog()->getPageCount();
    hash.addData(QByteArray::number(qint64(pageCount)));
    for (size_t i = 0; i < pageCount; ++i)
    {
        hash.addData(computePageFingerprint(document, PDFInteger(i)));
    }

    return hash.result();
}

PDFRenderer::Features PDFOCRPagePreparer::getRasterizationFeatures()
{
    return PDFRenderer::Features(PDFRenderer::Antialiasing | PDFRenderer::TextAntialiasing | PDFRenderer::SmoothImages | PDFRenderer::ClipToCropBox);
}

QSize PDFOCRPagePreparer::getRasterSize(const PDFPage* page, double dpi)
{
    // The user unit scales the user space, so the raster has the requested physical resolution (IMAGE-01)
    const double userUnit = page->getUserUnit() > 0.0 && std::isfinite(page->getUserUnit()) ? page->getUserUnit() : 1.0;
    const QSizeF sizeInPoints = page->getRotatedCropBox().size();
    const double scale = dpi * PDF_POINT_TO_INCH * userUnit;
    const double width = sizeInPoints.width() * scale;
    const double height = sizeInPoints.height() * scale;

    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0 ||
        width > double(std::numeric_limits<int>::max() / 4) || height > double(std::numeric_limits<int>::max() / 4))
    {
        return QSize();
    }

    return QSize(qMax(1, qRound(width)), qMax(1, qRound(height)));
}

double PDFOCRPagePreparer::getLimitedDpi(const PDFPage* page, double dpi, qint64 maximumPixels, int maximumDimension)
{
    // Physical size of the page: the user unit scales the user space (IMAGE-01, GEOM-01)
    const double userUnit = page->getUserUnit() > 0.0 && std::isfinite(page->getUserUnit()) ? page->getUserUnit() : 1.0;
    const QSizeF sizeInInches = page->getRotatedCropBox().size() * (PDF_POINT_TO_INCH * userUnit);
    const double areaInInches = sizeInInches.width() * sizeInInches.height();

    if (!std::isfinite(areaInInches) || areaInInches <= 0.0)
    {
        return dpi;
    }

    double limitedDpi = dpi;
    if (maximumPixels > 0 && areaInInches * dpi * dpi > double(maximumPixels))
    {
        limitedDpi = std::floor(std::sqrt(double(maximumPixels) / areaInInches));
    }

    const double longerSide = qMax(sizeInInches.width(), sizeInInches.height());
    if (maximumDimension > 0 && longerSide * limitedDpi > double(maximumDimension))
    {
        limitedDpi = std::min(limitedDpi, std::floor(double(maximumDimension) / longerSide));
    }

    if (limitedDpi >= dpi)
    {
        return dpi;
    }

    auto fits = [&](double candidate)
    {
        const QSize size = getRasterSize(page, candidate);
        return size.isValid() &&
               (maximumPixels <= 0 || qint64(size.width()) * qint64(size.height()) <= maximumPixels) &&
               (maximumDimension <= 0 || (size.width() <= maximumDimension && size.height() <= maximumDimension));
    };

    // Size of the raster is rounded, so the limit can still be exceeded by a few pixels
    for (int i = 0; i < 8 && limitedDpi > 1.0 && !fits(limitedDpi); ++i)
    {
        limitedDpi -= 1.0;
    }

    return limitedDpi;
}

qint64 PDFOCRPagePreparer::estimateRasterBytes(const PDFPage* page, double dpi)
{
    const QSize size = getRasterSize(page, dpi);
    return qint64(size.width()) * qint64(size.height()) * 4;
}

static QImage compositeOntoWhite(const QImage& image)
{
    QImage result(image.size(), QImage::Format_RGB32);
    result.fill(Qt::white);

    QPainter painter(&result);
    painter.drawImage(0, 0, image);
    painter.end();

    return result;
}

PDFOCRPagePreparer::RasterResult PDFOCRPagePreparer::rasterize(PDFInteger pageIndex,
                                                               double dpi,
                                                               const std::vector<QRectF>& maskedRectangles,
                                                               qint64 maximumPixels,
                                                               const PDFOperationControl* operationControl,
                                                               int maximumDimension) const
{
    RasterResult result;

    const PDFCatalog* catalog = m_document->getCatalog();
    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        result.error = PDFOCRError::create(PDFOCRErrorCode::RasterizationFailed, PDFTranslationContext::tr("Page %1 does not exist.").arg(pageIndex + 1), PDFTranslationContext::tr("Rasterization"));
        return result;
    }

    const PDFPage* page = catalog->getPage(pageIndex);

    PDFOCRPageGeometry& geometry = result.geometry;
    geometry.mediaBox = page->getMediaBox();
    geometry.cropBox = page->getCropBox();
    geometry.rotation = int(page->getPageRotation()) * 90;
    geometry.userUnit = page->getUserUnit();
    geometry.requestedDpi = dpi;
    geometry.dpi = getLimitedDpi(page, dpi, maximumPixels, maximumDimension);

    if (geometry.dpi < 1.0)
    {
        result.error = PDFOCRError::create(PDFOCRErrorCode::ImageTooLarge, PDFTranslationContext::tr("Page %1 is too large to be rasterized.").arg(pageIndex + 1), PDFTranslationContext::tr("Rasterization"));
        return result;
    }

    const QSize size = getRasterSize(page, geometry.dpi);
    if (size.isEmpty())
    {
        result.error = PDFOCRError::create(PDFOCRErrorCode::RasterizationFailed, PDFTranslationContext::tr("Page %1 has invalid size.").arg(pageIndex + 1), PDFTranslationContext::tr("Rasterization"));
        return result;
    }

    if ((qint64(size.width()) * qint64(size.height()) > maximumPixels && maximumPixels > 0) ||
        (maximumDimension > 0 && (size.width() > maximumDimension || size.height() > maximumDimension)))
    {
        result.error = PDFOCRError::create(PDFOCRErrorCode::ImageTooLarge, PDFTranslationContext::tr("Raster of the page %1 (%2 x %3 pixels) exceeds the limit. Select a lower resolution or a smaller region.").arg(pageIndex + 1).arg(size.width()).arg(size.height()), PDFTranslationContext::tr("Rasterization"));
        return result;
    }

    geometry.rasterSize = size;

    // Transformation from the canonical page space to the raster (R). The rotated
    // crop box is mapped onto the image rectangle.
    const PageRotation rotation = page->getPageRotation();
    const QRectF rotatedCropBox = page->getRotatedBox(page->getCropBox(), rotation);
    geometry.pageToRaster = PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedCropBox, QRectF(QPointF(0, 0), QSizeF(size)), rotation);
    geometry.rasterToEngine = QTransform();
    geometry.engineImageSize = size;
    geometry.pipeline << QStringLiteral("render(dpi=%1,size=%2x%3,rotation=%4)").arg(geometry.dpi).arg(size.width()).arg(size.height()).arg(geometry.rotation);

    if (PDFOperationControl::isOperationCancelled(operationControl))
    {
        result.error = PDFOCRError::create(PDFOCRErrorCode::Cancelled, PDFTranslationContext::tr("Operation was cancelled."), PDFTranslationContext::tr("Rasterization"));
        return result;
    }

    // Compile the page
    const PDFRenderer::Features features = getRasterizationFeatures();
    PDFPrecompiledPage compiledPage;
    PDFRenderer renderer(m_document, m_fontCache, m_cms, m_optionalContentActivity, features, m_meshQualitySettings);
    renderer.setOperationControl(operationControl);
    renderer.compile(&compiledPage, size_t(pageIndex));

    if (PDFOperationControl::isOperationCancelled(operationControl))
    {
        result.error = PDFOCRError::create(PDFOCRErrorCode::Cancelled, PDFTranslationContext::tr("Operation was cancelled."), PDFTranslationContext::tr("Rasterization"));
        return result;
    }

    for (const PDFRenderError& error : compiledPage.getErrors())
    {
        if (error.type == RenderErrorType::Error)
        {
            result.error = PDFOCRError::create(PDFOCRErrorCode::RasterizationFailed, PDFTranslationContext::tr("Page %1 cannot be rendered: %2").arg(pageIndex + 1).arg(error.message), PDFTranslationContext::tr("Rasterization"));
            return result;
        }
    }

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
    {
        result.error = PDFOCRError::create(PDFOCRErrorCode::OutOfMemory, PDFTranslationContext::tr("Not enough memory for the raster of the page %1 (%2 x %3 pixels).").arg(pageIndex + 1).arg(size.width()).arg(size.height()), PDFTranslationContext::tr("Rasterization"));
        return result;
    }
    image.fill(Qt::transparent);

    if (m_rendererEngine == RendererEngine::Blend2D_MultiThread || m_rendererEngine == RendererEngine::Blend2D_SingleThread)
    {
        PDFBLPaintDevice paintDevice(image, false);
        QPainter painter(&paintDevice);
        compiledPage.draw(&painter, page->getCropBox(), geometry.pageToRaster, features, 1.0);
        painter.end();
    }
    else
    {
        QPainter painter(&image);
        compiledPage.draw(&painter, page->getCropBox(), geometry.pageToRaster, features, 1.0);
        painter.end();
    }

    // Composite onto white background (IMAGE-02)
    result.image = compositeOntoWhite(image);
    if (result.image.isNull())
    {
        result.error = PDFOCRError::create(PDFOCRErrorCode::OutOfMemory, PDFTranslationContext::tr("Not enough memory for the image of the page %1.").arg(pageIndex + 1), PDFTranslationContext::tr("Rendering"));
        return result;
    }

    // Mask rectangles (annotations, unapplied redactions, IMAGE-03, PDF-13)
    if (!maskedRectangles.empty())
    {
        QPainter painter(&result.image);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.setRenderHint(QPainter::Antialiasing, false);
        for (const QRectF& rect : maskedRectangles)
        {
            const QRectF imageRect = geometry.pageToRaster.mapRect(rect);
            painter.drawRect(imageRect.adjusted(-1, -1, 1, 1));
        }
        painter.end();
        geometry.pipeline << QStringLiteral("mask(rectangles=%1)").arg(maskedRectangles.size());
    }

    result.image.setDotsPerMeterX(qRound(geometry.dpi / 0.0254));
    result.image.setDotsPerMeterY(qRound(geometry.dpi / 0.0254));

    return result;
}

PDFOCRPagePreparer::PreprocessResult PDFOCRPagePreparer::preprocess(const QImage& raster,
                                                                    const PDFOCRPageGeometry& inputGeometry,
                                                                    const PDFOCRPreprocessing& preprocessing,
                                                                    const std::optional<PDFOCROrientation>& detectedOrientation,
                                                                    const PDFOperationControl* operationControl)
{
    PreprocessResult result;
    result.geometry = inputGeometry;
    PDFOCRPageGeometry& geometry = result.geometry;

    QImage image = raster;
    QTransform rasterToEngine;

    auto checkCancelled = [&]()
    {
        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            result.error = PDFOCRError::create(PDFOCRErrorCode::Cancelled, PDFTranslationContext::tr("Operation was cancelled."), PDFTranslationContext::tr("Preprocessing"));
            return true;
        }
        return false;
    };

    // 1. Orientation: manual rotation, optionally replaced by the detected orientation.
    // Orientation is detected on the unrotated raster, so it is an absolute value, which
    // replaces the manual rotation (it is not added to it). Uncertain detection is not
    // used: a wrong rotation destroys the recognition of a correctly oriented page.
    int rotation = preprocessing.rotation;
    if (preprocessing.autoOrientation && detectedOrientation)
    {
        const bool isConfident = !detectedOrientation->confidence || *detectedOrientation->confidence >= MinimumOrientationConfidence;
        if (isConfident)
        {
            rotation = detectedOrientation->rotation;
        }
        else if (detectedOrientation->rotation != 0)
        {
            geometry.pipeline << QStringLiteral("orientation(ignored,rotation=%1,low-confidence=%2)").arg(detectedOrientation->rotation).arg(*detectedOrientation->confidence, 0, 'f', 1);
        }
    }
    rotation = ((rotation % 360) + 360) % 360;

    if (rotation != 0)
    {
        QTransform rotationMatrix;
        rotationMatrix.rotate(rotation);
        const QTransform trueMatrix = QImage::trueMatrix(rotationMatrix, image.width(), image.height());
        image = image.transformed(rotationMatrix, Qt::FastTransformation);
        rasterToEngine = rasterToEngine * trueMatrix;
        geometry.pipeline << QStringLiteral("rotate(%1)").arg(rotation);
    }

    if (checkCancelled())
    {
        return result;
    }

    // 2. Deskew
    if (preprocessing.deskew)
    {
        double angle = 0.0;
        double confidence = 0.0;

        if (detectedOrientation && !qFuzzyIsNull(detectedOrientation->deskewAngle))
        {
            angle = detectedOrientation->deskewAngle;
            confidence = detectedOrientation->deskewConfidence.value_or(50.0);
        }
        else
        {
            angle = estimateSkewAngle(image, &confidence, operationControl);
        }

        if (checkCancelled())
        {
            return result;
        }

        if (std::abs(angle) >= 0.1 && std::abs(angle) <= 5.0 && confidence >= 30.0)
        {
            // Angle is the skew of the content (positive clockwise), the image
            // is straightened by the rotation by the opposite angle.
            QTransform deskewMatrix;
            deskewMatrix.rotate(-angle);
            const QTransform trueMatrix = QImage::trueMatrix(deskewMatrix, image.width(), image.height());
            QImage rotated = image.transformed(deskewMatrix, Qt::SmoothTransformation);

            // Transparent corners must become white
            image = compositeOntoWhite(rotated.convertToFormat(QImage::Format_ARGB32_Premultiplied));
            if (image.isNull())
            {
                result.error = PDFOCRError::create(PDFOCRErrorCode::OutOfMemory, PDFTranslationContext::tr("Not enough memory for the straightened image."), PDFTranslationContext::tr("Preprocessing"));
                return result;
            }
            rasterToEngine = rasterToEngine * trueMatrix;
            geometry.pipeline << QStringLiteral("deskew(angle=%1,confidence=%2)").arg(angle, 0, 'f', 2).arg(confidence, 0, 'f', 0);
        }
        else
        {
            geometry.pipeline << QStringLiteral("deskew(skipped,angle=%1,confidence=%2)").arg(angle, 0, 'f', 2).arg(confidence, 0, 'f', 0);
        }
    }

    // 3. Photometric filters
    const bool needsGrayscale = preprocessing.grayscale || preprocessing.denoise || preprocessing.invert || preprocessing.binarization == PDFOCRBinarization::Otsu;
    if (needsGrayscale)
    {
        image = toGrayscale(image);
        geometry.pipeline << QStringLiteral("grayscale");
    }

    if (checkCancelled())
    {
        return result;
    }

    if (preprocessing.invert)
    {
        image.invertPixels();
        geometry.pipeline << QStringLiteral("invert");
    }

    if (preprocessing.denoise)
    {
        image = medianFilter(image, operationControl);
        geometry.pipeline << QStringLiteral("median(3x3)");
    }

    if (checkCancelled())
    {
        return result;
    }

    if (preprocessing.binarization == PDFOCRBinarization::Otsu)
    {
        image = otsuBinarization(image, operationControl);
        geometry.pipeline << QStringLiteral("otsu");
    }

    if (checkCancelled())
    {
        return result;
    }

    geometry.rasterToEngine = rasterToEngine;
    geometry.engineImageSize = image.size();
    result.image = image;
    return result;
}

QImage PDFOCRPagePreparer::maskRegions(QImage image, const QTransform& pageToImage, const std::vector<PDFOCRRegion>& regions)
{
    bool hasInclusive = false;
    for (const PDFOCRRegion& region : regions)
    {
        if (region.type == PDFOCRRegionType::Recognize)
        {
            hasInclusive = true;
            break;
        }
    }

    if (regions.empty())
    {
        return image;
    }

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);

    if (hasInclusive)
    {
        QPainterPath outside;
        outside.addRect(QRectF(QPointF(0, 0), QSizeF(image.size())));

        QPainterPath inclusive;
        for (const PDFOCRRegion& region : regions)
        {
            if (region.type == PDFOCRRegionType::Recognize)
            {
                inclusive.addRect(pageToImage.mapRect(region.rect));
            }
        }

        painter.drawPath(outside.subtracted(inclusive));
    }

    for (const PDFOCRRegion& region : regions)
    {
        if (region.type == PDFOCRRegionType::Exclude)
        {
            painter.drawRect(pageToImage.mapRect(region.rect));
        }
    }

    painter.end();
    return image;
}

std::vector<std::pair<int, QRect>> PDFOCRPagePreparer::getRecognitionRectangles(const QSize& imageSize,
                                                                                 const QTransform& pageToImage,
                                                                                 const std::vector<PDFOCRRegion>& regions)
{
    std::vector<std::pair<int, QRect>> result;
    const QRect imageRect(QPoint(0, 0), imageSize);

    std::vector<const PDFOCRRegion*> inclusiveRegions;
    for (const PDFOCRRegion& region : regions)
    {
        if (region.type == PDFOCRRegionType::Recognize)
        {
            inclusiveRegions.push_back(&region);
        }
    }

    std::stable_sort(inclusiveRegions.begin(), inclusiveRegions.end(), [](const PDFOCRRegion* left, const PDFOCRRegion* right) { return left->order < right->order; });

    for (const PDFOCRRegion* region : inclusiveRegions)
    {
        // Context margin around the region (REGION-05)
        const QRect rect = pageToImage.mapRect(region->rect).toAlignedRect().adjusted(-2, -2, 2, 2).intersected(imageRect);
        if (rect.isEmpty())
        {
            continue;
        }

        // Overlapping inclusive regions with the same configuration are united, so no
        // point of the page is recognized twice because of the overlap (REGION-01,
        // REGION-02). Regions with a different configuration are recognized separately;
        // the user resolves such overlaps before the run.
        bool united = false;
        for (auto& item : result)
        {
            const PDFOCRRegion* existing = nullptr;
            for (const PDFOCRRegion* candidate : inclusiveRegions)
            {
                if (candidate->id == item.first)
                {
                    existing = candidate;
                    break;
                }
            }

            if (existing && item.second.intersects(rect) && existing->configuration == region->configuration)
            {
                item.second = item.second.united(rect);
                united = true;
                break;
            }
        }

        if (!united)
        {
            result.emplace_back(region->id, rect);
        }
    }

    if (result.empty() && inclusiveRegions.empty())
    {
        result.emplace_back(-1, imageRect);
    }

    return result;
}

bool PDFOCRPagePreparer::isBlankImage(const QImage& image, double* inkRatio, const PDFOperationControl* operationControl)
{
    if (image.isNull())
    {
        if (inkRatio)
        {
            *inkRatio = 0.0;
        }
        return true;
    }

    const QImage grayscale = toGrayscale(image);
    qint64 darkPixels = 0;
    const qint64 totalPixels = qint64(grayscale.width()) * qint64(grayscale.height());

    for (int y = 0; y < grayscale.height(); ++y)
    {
        if ((y & 63) == 0 && PDFOperationControl::isOperationCancelled(operationControl))
        {
            return false;
        }

        const uchar* line = grayscale.constScanLine(y);
        for (int x = 0; x < grayscale.width(); ++x)
        {
            if (line[x] < 128)
            {
                ++darkPixels;
            }
        }
    }

    const double ratio = totalPixels > 0 ? double(darkPixels) / double(totalPixels) : 0.0;
    if (inkRatio)
    {
        *inkRatio = ratio;
    }

    return ratio < 0.0005;
}

double PDFOCRPagePreparer::estimateSkewAngle(const QImage& image, double* confidence, const PDFOperationControl* operationControl)
{
    if (confidence)
    {
        *confidence = 0.0;
    }

    if (image.isNull())
    {
        return 0.0;
    }

    // Downscale the image to speed up the estimation
    QImage grayscale = toGrayscale(image);
    constexpr int TargetWidth = 800;
    if (grayscale.width() > TargetWidth)
    {
        grayscale = grayscale.scaledToWidth(TargetWidth, Qt::SmoothTransformation);
    }

    const int width = grayscale.width();
    const int height = grayscale.height();
    if (width < 10 || height < 10)
    {
        return 0.0;
    }

    // Collect dark pixels
    std::vector<QPoint> darkPixels;
    for (int y = 0; y < height; ++y)
    {
        const uchar* line = grayscale.constScanLine(y);
        for (int x = 0; x < width; ++x)
        {
            if (line[x] < 128)
            {
                darkPixels.emplace_back(x, y);
            }
        }
    }

    if (darkPixels.size() < 100)
    {
        return 0.0;
    }

    // Projection profile: for each candidate angle, compute the variance of the
    // row histogram. The angle with the maximal variance is the skew angle.
    double bestAngle = 0.0;
    double bestScore = -1.0;
    double zeroScore = 0.0;
    std::vector<double> scores;

    const double centerX = width * 0.5;
    const double centerY = height * 0.5;
    std::vector<int> histogram(size_t(height * 2), 0);

    for (double angle = -5.0; angle <= 5.0 + 1e-9; angle += 0.25)
    {
        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            return 0.0;
        }

        std::fill(histogram.begin(), histogram.end(), 0);
        const double radians = qDegreesToRadians(angle);
        const double sinA = std::sin(radians);
        const double cosA = std::cos(radians);

        for (const QPoint& point : darkPixels)
        {
            const double y = (point.x() - centerX) * sinA + (point.y() - centerY) * cosA + centerY;
            const int row = int(y) + height / 2;
            if (row >= 0 && row < int(histogram.size()))
            {
                ++histogram[size_t(row)];
            }
        }

        double sum = 0.0;
        double sumSquares = 0.0;
        for (int value : histogram)
        {
            sum += value;
            sumSquares += double(value) * double(value);
        }

        const double n = double(histogram.size());
        const double mean = sum / n;
        const double variance = sumSquares / n - mean * mean;
        scores.push_back(variance);

        if (qFuzzyIsNull(angle))
        {
            zeroScore = variance;
        }

        if (variance > bestScore)
        {
            bestScore = variance;
            bestAngle = angle;
        }
    }

    if (confidence)
    {
        // Confidence: relative improvement against zero angle, scaled to 0-100
        if (zeroScore > 0.0 && bestScore > zeroScore)
        {
            *confidence = qBound(0.0, 100.0 * (bestScore - zeroScore) / zeroScore * 2.0, 100.0);
        }
        else
        {
            *confidence = 0.0;
        }
    }

    // Projection uses the mapping of QTransform::rotate(angle), so the rotation by
    // bestAngle makes the lines horizontal. The skew of the content is the opposite
    // angle (positive clockwise in the image, see PDFOCROrientation::deskewAngle).
    return -bestAngle;
}

PDFOCRQuad PDFOCRPagePreparer::imageRectToPageQuad(const QRectF& rect, const QTransform& imageToPage)
{
    // In image space, y grows downwards, so the bottom edge has the maximal y.
    QPolygonF polygon;
    polygon << rect.bottomLeft() << rect.bottomRight() << rect.topRight() << rect.topLeft();
    return imagePolygonToPageQuad(polygon, imageToPage);
}

PDFOCRQuad PDFOCRPagePreparer::imagePolygonToPageQuad(const QPolygonF& polygon, const QTransform& imageToPage)
{
    PDFOCRQuad quad;
    if (polygon.size() < 4)
    {
        return quad;
    }

    for (int i = 0; i < 4; ++i)
    {
        quad.points[size_t(i)] = imageToPage.map(polygon[i]);
    }

    return quad;
}

/// Intersection over union of two rectangles. Unlike the ratio to the smaller
/// rectangle, a small word inside of a large (wrong) box is not a duplicate.
static double getIntersectionOverUnion(const QRectF& first, const QRectF& second)
{
    const QRectF intersection = first.intersected(second);
    if (intersection.isEmpty())
    {
        return 0.0;
    }

    const double intersectionArea = intersection.width() * intersection.height();
    const double unionArea = first.width() * first.height() + second.width() * second.height() - intersectionArea;
    return unionArea > 0.0 ? intersectionArea / unionArea : 0.0;
}

bool PDFOCRPagePreparer::intersectsAny(const QRectF& rect, const std::vector<QRectF>& rectangles)
{
    for (const QRectF& other : rectangles)
    {
        const QRectF intersection = rect.intersected(other);
        if (intersection.width() > 0.0 && intersection.height() > 0.0)
        {
            return true;
        }
    }
    return false;
}

void PDFOCRPagePreparer::updateExcludedRegionFlags(PDFOCRPageResult& result)
{
    std::vector<QRectF> excludedRectangles = result.analysis.redactionRectangles;
    for (const PDFOCRRegion& region : result.regions)
    {
        if (region.type == PDFOCRRegionType::Exclude)
        {
            excludedRectangles.push_back(region.rect);
        }
    }

    for (PDFOCRWord* word : result.getWords())
    {
        word->overlapsExcludedRegion = intersectsAny(word->quad.boundingRect(), excludedRectangles);
    }
}

void PDFOCRPagePreparer::appendOutput(PDFOCRPageResult& result,
                                      const PDFOCRRecognitionOutput& output,
                                      const PDFOCRPageGeometry& geometry,
                                      int regionId,
                                      const std::vector<QRectF>& excludedRectangles,
                                      const QTransform& outputToEngine)
{
    const QTransform imageToPage = outputToEngine * geometry.getEngineToPage();

    // Existing words (for the deduplication, REGION-05). Only the words of the
    // previous recognitions (other regions) are compared, words of this output
    // are never duplicates of each other.
    std::vector<QRectF> existingWords;
    for (const PDFOCRWord* word : result.getWords())
    {
        existingWords.push_back(word->quad.boundingRect());
    }

    for (const PDFOCRRawBlock& rawBlock : output.blocks)
    {
        PDFOCRBlock block;
        block.id = result.allocateId();
        block.type = rawBlock.type;
        block.regionId = regionId;
        block.quad = imageRectToPageQuad(rawBlock.rect, imageToPage);

        for (const PDFOCRRawLine& rawLine : rawBlock.lines)
        {
            PDFOCRLine line;
            line.id = result.allocateId();
            line.direction = rawLine.direction;
            line.quad = rawLine.polygon.size() >= 4 ? imagePolygonToPageQuad(rawLine.polygon, imageToPage) : imageRectToPageQuad(rawLine.rect, imageToPage);

            if (!rawLine.baseline.isNull())
            {
                line.baseline = QLineF(imageToPage.map(rawLine.baseline.p1()), imageToPage.map(rawLine.baseline.p2()));
            }
            else
            {
                line.baseline = QLineF(line.quad.points[0], line.quad.points[1]);
            }

            if (rawLine.rawConfidence)
            {
                line.confidence = PDFOCRConfidence::fromRaw(*rawLine.rawConfidence, output.rawConfidenceMinimum, output.rawConfidenceMaximum, PDFOCRConfidenceLevel::Line);
            }

            std::vector<PDFOCRRawWord> rawWords = rawLine.words;
            if (rawWords.empty() && !rawLine.text.trimmed().isEmpty())
            {
                // Engine provides only the line text: the whole line is a single text segment
                PDFOCRRawWord segment;
                segment.text = rawLine.text;
                segment.rect = rawLine.rect;
                segment.polygon = rawLine.polygon;
                segment.rawConfidence = rawLine.rawConfidence;
                rawWords.push_back(std::move(segment));
            }

            for (const PDFOCRRawWord& rawWord : rawWords)
            {
                if (rawWord.text.trimmed().isEmpty())
                {
                    continue;
                }

                PDFOCRWord word;
                word.id = result.allocateId();
                word.originalText = rawWord.text;
                word.text = rawWord.text;
                word.quad = rawWord.polygon.size() >= 4 ? imagePolygonToPageQuad(rawWord.polygon, imageToPage) : imageRectToPageQuad(rawWord.rect, imageToPage);
                word.geometryOrigin = PDFOCRGeometryOrigin::Engine;
                word.textOrigin = PDFOCRTextOrigin::OCR;
                word.language = rawWord.language;

                if (rawWord.rawConfidence && (output.confidenceLevel == PDFOCRConfidenceLevel::Word || output.confidenceLevel == PDFOCRConfidenceLevel::Symbol))
                {
                    word.confidence = PDFOCRConfidence::fromRaw(*rawWord.rawConfidence, output.rawConfidenceMinimum, output.rawConfidenceMaximum, PDFOCRConfidenceLevel::Word);
                }
                else if (rawLine.rawConfidence && output.confidenceLevel == PDFOCRConfidenceLevel::Line)
                {
                    // Line confidence is shared by the words of the line; the level is
                    // preserved, so the UI does not present it as a word confidence (CONF-01).
                    word.confidence = PDFOCRConfidence::fromRaw(*rawLine.rawConfidence, output.rawConfidenceMinimum, output.rawConfidenceMaximum, PDFOCRConfidenceLevel::Line);
                }

                const QRectF wordRect = word.quad.boundingRect();

                // Deduplication (REGION-05): word is skipped, if it substantially
                // overlaps an already present word.
                bool duplicate = false;
                for (const QRectF& existing : existingWords)
                {
                    if (getIntersectionOverUnion(existing, wordRect) > 0.5)
                    {
                        duplicate = true;
                        break;
                    }
                }

                if (duplicate)
                {
                    continue;
                }

                // Excluded regions (REGION-05): any overlap with a positive area
                word.overlapsExcludedRegion = intersectsAny(wordRect, excludedRectangles);

                line.words.push_back(std::move(word));
            }

            if (!line.words.empty())
            {
                block.lines.push_back(std::move(line));
            }
        }

        if (!block.lines.empty())
        {
            result.blocks.push_back(std::move(block));
        }
    }
}

QImage PDFOCRPagePreparer::toGrayscale(const QImage& image)
{
    if (image.format() == QImage::Format_Grayscale8)
    {
        return image;
    }

    return image.convertToFormat(QImage::Format_Grayscale8);
}

QImage PDFOCRPagePreparer::medianFilter(const QImage& grayscale, const PDFOperationControl* operationControl)
{
    const QImage source = toGrayscale(grayscale);
    const int width = source.width();
    const int height = source.height();

    if (width < 3 || height < 3)
    {
        return source;
    }

    QImage result(source.size(), QImage::Format_Grayscale8);

    for (int y = 0; y < height; ++y)
    {
        if ((y & 63) == 0 && PDFOperationControl::isOperationCancelled(operationControl))
        {
            return source;
        }

        const uchar* previous = source.constScanLine(qMax(y - 1, 0));
        const uchar* current = source.constScanLine(y);
        const uchar* next = source.constScanLine(qMin(y + 1, height - 1));
        uchar* target = result.scanLine(y);

        for (int x = 0; x < width; ++x)
        {
            const int left = qMax(x - 1, 0);
            const int right = qMin(x + 1, width - 1);

            std::array<uchar, 9> values =
            {
                previous[left], previous[x], previous[right],
                current[left], current[x], current[right],
                next[left], next[x], next[right]
            };

            std::nth_element(values.begin(), values.begin() + 4, values.end());
            target[x] = values[4];
        }
    }

    return result;
}

QImage PDFOCRPagePreparer::otsuBinarization(const QImage& grayscale, const PDFOperationControl* operationControl)
{
    const QImage source = toGrayscale(grayscale);
    const int width = source.width();
    const int height = source.height();

    std::array<qint64, 256> histogram = { };
    for (int y = 0; y < height; ++y)
    {
        if ((y & 63) == 0 && PDFOperationControl::isOperationCancelled(operationControl))
        {
            return source;
        }

        const uchar* line = source.constScanLine(y);
        for (int x = 0; x < width; ++x)
        {
            ++histogram[line[x]];
        }
    }

    const qint64 total = qint64(width) * qint64(height);
    if (total <= 0)
    {
        return source;
    }

    double sum = 0.0;
    for (int i = 0; i < 256; ++i)
    {
        sum += double(i) * double(histogram[size_t(i)]);
    }

    double sumBackground = 0.0;
    qint64 weightBackground = 0;
    double maximumVariance = -1.0;
    int threshold = 128;

    for (int t = 0; t < 256; ++t)
    {
        weightBackground += histogram[size_t(t)];
        if (weightBackground == 0)
        {
            continue;
        }

        const qint64 weightForeground = total - weightBackground;
        if (weightForeground == 0)
        {
            break;
        }

        sumBackground += double(t) * double(histogram[size_t(t)]);
        const double meanBackground = sumBackground / double(weightBackground);
        const double meanForeground = (sum - sumBackground) / double(weightForeground);
        const double variance = double(weightBackground) * double(weightForeground) * (meanBackground - meanForeground) * (meanBackground - meanForeground);

        if (variance > maximumVariance)
        {
            maximumVariance = variance;
            threshold = t;
        }
    }

    QImage result(source.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y)
    {
        const uchar* line = source.constScanLine(y);
        uchar* target = result.scanLine(y);
        for (int x = 0; x < width; ++x)
        {
            target[x] = line[x] > threshold ? 255 : 0;
        }
    }

    return result;
}

bool PDFOCRPagePreparer::isDarkBackground(const QImage& grayscale)
{
    const QImage source = toGrayscale(grayscale);
    if (source.isNull())
    {
        return false;
    }

    qint64 sum = 0;
    qint64 count = 0;
    for (int y = 0; y < source.height(); y += 4)
    {
        const uchar* line = source.constScanLine(y);
        for (int x = 0; x < source.width(); x += 4)
        {
            sum += line[x];
            ++count;
        }
    }

    return count > 0 && (sum / count) < 96;
}

}   // namespace pdf
