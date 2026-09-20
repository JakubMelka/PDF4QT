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

        QRectF boundingRect = info.outline.boundingRect();
        if (boundingRect.isEmpty())
        {
            boundingRect = QRectF(0.0, 0.0, info.advance, info.fontSize);
        }

        VisibleCharacter character;
        character.rect = info.matrix.mapRect(boundingRect);
        m_visibleCharacters.push_back(character);
        ++visibleCharacterCount;
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
};

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

PDFOCRPagePreparer::PolicyDecision PDFOCRPagePreparer::evaluateExistingTextPolicy(const PDFOCRPageAnalysis& analysis,
                                                                                 PDFOCRExistingTextPolicy policy,
                                                                                 bool hasInclusiveRegions,
                                                                                 QString* reason)
{
    auto decide = [reason](PolicyDecision decision, const QString& text)
    {
        if (reason)
        {
            *reason = text;
        }
        return decision;
    };

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

    // Own OCR layer
    PDFOCRTextLayerWriter::LayerInfo layerInfo = PDFOCRTextLayerWriter::readLayerInfo(m_document, pageIndex);
    if (layerInfo.isPresent)
    {
        analysis.hasOwnOCRLayer = true;
        analysis.ownLayerId = layerInfo.layerId;

        if (!layerInfo.fingerprintMatches)
        {
            analysis.notes << PDFTranslationContext::tr("Own OCR layer metadata do not match the current page content (page was modified by another tool).");
        }
    }

    analysis.isTagged = isTaggedDocument(m_document);

    // Classification
    constexpr int UsableTextThreshold = 40;
    const bool usableVisibleText = analyzer.visibleCharacterCount - analyzer.coveredCharacterCount >= UsableTextThreshold;
    const bool hasContent = analyzer.visibleCharacterCount > 0 || analyzer.invisibleCharacterCount > 0 || analyzer.imageCount > 0 || analyzer.pathCount > 0;

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
    else if (analyzer.invisibleCharacterCount >= UsableTextThreshold)
    {
        analysis.contentClass = PDFOCRPageContentClass::InvisibleText;
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
        analysis.notes << PDFTranslationContext::tr("Page contains a small amount of digital text (%n character(s)), for example a page number.", nullptr, analyzer.visibleCharacterCount);
    }

    if (analyzer.invisibleCharacterCount > 0 && !analysis.hasOwnOCRLayer)
    {
        analysis.notes << PDFTranslationContext::tr("Page contains invisible text of foreign origin (%n character(s)).", nullptr, analyzer.invisibleCharacterCount);
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

    const QByteArray metadata = document->getDecodedStream(metadataObject.getStream());
    bool result = false;

    if (metadata.contains("http://www.aiim.org/pdfa/ns/id/") || metadata.contains("pdfaid:part"))
    {
        result = true;
        if (declarations)
        {
            *declarations << QStringLiteral("PDF/A");
        }
    }

    if (metadata.contains("http://www.aiim.org/pdfua/ns/id/") || metadata.contains("pdfuaid:part"))
    {
        result = true;
        if (declarations)
        {
            *declarations << QStringLiteral("PDF/UA");
        }
    }

    return result;
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
        const PDFObjectReference reference = object.getReference();
        if (depth <= 0 || visited.count(reference))
        {
            return QByteArrayLiteral("R");
        }
        visited.insert(reference);
        return digestObject(storage->getObjectByReference(reference), storage, depth - 1, visited, skipKey);
    }

    if (object.isStream())
    {
        const PDFStream* stream = object.getStream();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArrayLiteral("S"));
        hash.addData(digestObject(PDFObject::createDictionary(std::make_shared<PDFDictionary>(*stream->getDictionary())), storage, depth, visited, skipKey));
        const QByteArray* content = stream->getContent();
        hash.addData(QByteArray::number(content->size()));
        hash.addData(content->left(4096));
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

            const QByteArray digest = digestObject(dictionary->getValue(i), storage, depth, visited, skipKey);
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
            hash.addData(digestObject(array->getItem(i), storage, depth, visited, skipKey));
        }
        return hash.result();
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
    hash.addData(QByteArrayLiteral("PDF4QT-OCR-PAGE-FINGERPRINT-1"));

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

    // Own OCR layer is excluded from the fingerprint
    const PDFObjectReference ownContentReference = PDFOCRTextLayerWriter::getOwnLayerContentReference(document, pageIndex);

    // Content streams
    std::vector<PDFObjectReference> contentReferences;
    std::vector<const PDFStream*> contentStreams;
    const PDFObject& contents = page->getContents();
    const PDFObject& dereferencedContents = document->getObject(contents);
    if (dereferencedContents.isArray())
    {
        const PDFArray* array = dereferencedContents.getArray();
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            const PDFObject& item = array->getItem(i);
            if (item.isReference() && ownContentReference.isValid() && item.getReference() == ownContentReference)
            {
                continue;
            }

            const PDFObject& dereferencedItem = document->getObject(item);
            if (dereferencedItem.isStream())
            {
                contentStreams.push_back(dereferencedItem.getStream());
            }
        }
    }
    else if (dereferencedContents.isStream())
    {
        if (!(contents.isReference() && ownContentReference.isValid() && contents.getReference() == ownContentReference))
        {
            contentStreams.push_back(dereferencedContents.getStream());
        }
    }

    for (const PDFStream* stream : contentStreams)
    {
        const QByteArray data = document->getDecodedStream(stream);
        hash.addData(QByteArray::number(data.size()));
        hash.addData(data);
    }

    // Resources (own font is excluded)
    std::set<PDFObjectReference> visited;
    auto skipKey = [](const QByteArray& key)
    {
        return key.startsWith(PDFOCRTextLayerWriter::FONT_RESOURCE_PREFIX);
    };
    hash.addData(digestObject(page->getResources(), storage, 3, visited, skipKey));

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
    const QSizeF sizeInPoints = page->getRotatedCropBox().size();
    const double scale = dpi * PDF_POINT_TO_INCH;
    const double width = sizeInPoints.width() * scale;
    const double height = sizeInPoints.height() * scale;

    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0 ||
        width > double(std::numeric_limits<int>::max() / 4) || height > double(std::numeric_limits<int>::max() / 4))
    {
        return QSize();
    }

    return QSize(qMax(1, qRound(width)), qMax(1, qRound(height)));
}

double PDFOCRPagePreparer::getLimitedDpi(const PDFPage* page, double dpi, qint64 maximumPixels)
{
    const QSizeF sizeInPoints = page->getRotatedCropBox().size();
    const double areaInInches = sizeInPoints.width() * sizeInPoints.height() * PDF_POINT_TO_INCH * PDF_POINT_TO_INCH;

    if (!std::isfinite(areaInInches) || areaInInches <= 0.0 || maximumPixels <= 0)
    {
        return dpi;
    }

    const double pixels = areaInInches * dpi * dpi;
    if (pixels <= double(maximumPixels))
    {
        return dpi;
    }

    return std::floor(std::sqrt(double(maximumPixels) / areaInInches));
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
                                                               const PDFOperationControl* operationControl) const
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
    geometry.dpi = getLimitedDpi(page, dpi, maximumPixels);

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

    if (qint64(size.width()) * qint64(size.height()) > maximumPixels && maximumPixels > 0)
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

    // 1. Orientation: manual rotation, optionally replaced by the detected orientation
    int rotation = preprocessing.rotation;
    if (preprocessing.autoOrientation && detectedOrientation && detectedOrientation->rotation != 0)
    {
        rotation = (rotation + detectedOrientation->rotation) % 360;
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
            QTransform deskewMatrix;
            deskewMatrix.rotate(angle);
            const QTransform trueMatrix = QImage::trueMatrix(deskewMatrix, image.width(), image.height());
            QImage rotated = image.transformed(deskewMatrix, Qt::SmoothTransformation);

            // Transparent corners must become white
            image = compositeOntoWhite(rotated.convertToFormat(QImage::Format_ARGB32_Premultiplied));
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
        result.emplace_back(region->id, rect);
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

    // Rotating the image by -bestAngle (counterclockwise in image coordinates
    // means positive in QTransform, which has y axis growing downwards) fixes the skew.
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

static double getOverlapRatio(const QRectF& first, const QRectF& second)
{
    const QRectF intersection = first.intersected(second);
    if (intersection.isEmpty())
    {
        return 0.0;
    }

    const double smaller = qMin(first.width() * first.height(), second.width() * second.height());
    if (smaller <= 0.0)
    {
        return 0.0;
    }

    return intersection.width() * intersection.height() / smaller;
}

void PDFOCRPagePreparer::appendOutput(PDFOCRPageResult& result,
                                      const PDFOCRRecognitionOutput& output,
                                      const PDFOCRPageGeometry& geometry,
                                      int regionId,
                                      const std::vector<QRectF>& excludedRectangles)
{
    const QTransform imageToPage = geometry.getEngineToPage();

    // Existing words (for the deduplication, REGION-05)
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
                    if (getOverlapRatio(existing, wordRect) > 0.6)
                    {
                        duplicate = true;
                        break;
                    }
                }

                if (duplicate)
                {
                    continue;
                }

                // Excluded regions (REGION-05)
                for (const QRectF& excluded : excludedRectangles)
                {
                    if (getOverlapRatio(excluded, wordRect) > 0.05)
                    {
                        word.overlapsExcludedRegion = true;
                        break;
                    }
                }

                existingWords.push_back(wordRect);
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
