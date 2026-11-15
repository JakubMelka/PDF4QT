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

#include "pdfdocumenttextflow.h"
#include "pdfdocument.h"
#include "pdfstructuretree.h"
#include "pdfexecutionpolicy.h"
#include "pdfconstants.h"
#include "pdfcms.h"
#include "pdfoperationcontrol.h"
#include "pdftextlayoutgenerator.h"
#include "pdfpagecontentprocessor.h"
#include "pdfprogress.h"
#include "pdfdbgheap.h"

#include <set>

namespace pdf
{

class PDFStructureTreeReferenceCollector : public PDFStructureTreeAbstractVisitor
{
public:
    explicit inline PDFStructureTreeReferenceCollector(std::map<PDFObjectReference, const PDFStructureItem*>* mapping) :
        m_mapping(mapping)
    {

    }

    virtual void visitStructureTree(const PDFStructureTree* structureTree) override;
    virtual void visitStructureElement(const PDFStructureElement* structureElement) override;
    virtual void visitStructureMarkedContentReference(const PDFStructureMarkedContentReference* structureMarkedContentReference) override;
    virtual void visitStructureObjectReference(const PDFStructureObjectReference* structureObjectReference) override;

private:
    void addReference(const PDFStructureItem* structureObjectReference);

    std::map<PDFObjectReference, const PDFStructureItem*>* m_mapping;
};

void PDFStructureTreeReferenceCollector::visitStructureTree(const PDFStructureTree* structureTree)
{
    addReference(structureTree);
    acceptChildren(structureTree);
}

void PDFStructureTreeReferenceCollector::visitStructureElement(const PDFStructureElement* structureElement)
{
    addReference(structureElement);
    acceptChildren(structureElement);
}

void PDFStructureTreeReferenceCollector::visitStructureMarkedContentReference(const PDFStructureMarkedContentReference* structureMarkedContentReference)
{
    addReference(structureMarkedContentReference);
    acceptChildren(structureMarkedContentReference);
}

void PDFStructureTreeReferenceCollector::visitStructureObjectReference(const PDFStructureObjectReference* structureObjectReference)
{
    addReference(structureObjectReference);
    acceptChildren(structureObjectReference);
}

void PDFStructureTreeReferenceCollector::addReference(const PDFStructureItem* structureItem)
{
    if (structureItem->getSelfReference().isValid())
    {
        (*m_mapping)[structureItem->getSelfReference()] = structureItem;
    }
}

struct PDFStructureTreeTextItem
{
    enum class Type
    {
        StartTag,
        EndTag,
        Text
    };

    PDFStructureTreeTextItem() = default;
    PDFStructureTreeTextItem(Type type, const PDFStructureItem* item, QString text, PDFInteger pageIndex, QRectF boundingRect, std::vector<QRectF> characterBoundingRects) :
        type(type), item(item), text(qMove(text)), pageIndex(pageIndex), boundingRect(boundingRect), characterBoundingRects(std::move(characterBoundingRects))
    {

    }

    static PDFStructureTreeTextItem createText(QString text, PDFInteger pageIndex, QRectF boundingRect, std::vector<QRectF> characterBoundingRects) { return PDFStructureTreeTextItem(Type::Text, nullptr, qMove(text), pageIndex, boundingRect, std::move(characterBoundingRects)); }
    static PDFStructureTreeTextItem createStartTag(const PDFStructureItem* item) { return PDFStructureTreeTextItem(Type::StartTag, item, QString(), -1, QRectF(), { }); }
    static PDFStructureTreeTextItem createEndTag(const PDFStructureItem* item) { return PDFStructureTreeTextItem(Type::EndTag, item, QString(), -1, QRectF(), { }); }

    Type type = Type::Text;
    const PDFStructureItem* item = nullptr;
    QString text;
    PDFInteger pageIndex = -1;
    QRectF boundingRect;
    std::vector<QRectF> characterBoundingRects;
    bool isArtifact = false;    ///< The text of a marked content /Artifact (PDFStructureTreeTextExtractor::MarkArtifacts)
    QString language;           ///< /Lang of the innermost marked content of the text (PDFStructureTreeTextExtractor::MarkLanguages)
};

using PDFStructureTreeTextSequence = std::vector<PDFStructureTreeTextItem>;

/// Helpers of the text flow
class PDFDocumentTextFlowHelper
{
public:
    PDFDocumentTextFlowHelper() = delete;

    /// /Lang of the property list of a marked content (ISO 32000-2 14.9.2, 14.6.2: the property
    /// list is inline or a name of the resource /Properties); empty = none
    static QString getMarkedContentLanguage(const PDFDocument* document, const PDFObject& properties, const PDFDictionary* propertiesDictionary)
    {
        PDFObject object = properties;
        if (object.isName())
        {
            object = propertiesDictionary ? propertiesDictionary->get(object.getString()) : PDFObject();
        }
        object = document ? document->getObject(object) : PDFObject();
        if (!object.isDictionary())
        {
            return QString();
        }

        PDFDocumentDataLoaderDecorator loader(document);
        return loader.readTextStringFromDictionary(object.getDictionary(), "Lang", QString()).trimmed();
    }

    /// The page marks of the order of the structure (B76): a mark "Page N" before the first item of
    /// each run of the items of one page; the structure may go back to a page, which gets its mark
    /// again (the listener hears, where the reading continues)
    static PDFDocumentTextFlow::Items insertPageMarks(PDFDocumentTextFlow::Items items)
    {
        PDFDocumentTextFlow::Items result;
        result.reserve(items.size() + 16);
        PDFInteger lastPageIndex = -1;
        for (PDFDocumentTextFlow::Item& item : items)
        {
            if (item.pageIndex >= 0 && item.pageIndex != lastPageIndex && !item.text.trimmed().isEmpty())
            {
                lastPageIndex = item.pageIndex;
                result.push_back(PDFDocumentTextFlow::Item{ QRectF(), item.pageIndex, PDFTranslationContext::tr("Page %1").arg(item.pageIndex + 1), PDFDocumentTextFlow::PageStart, {} });
            }
            result.push_back(std::move(item));
        }
        return result;
    }

    /// The innermost language of the stack of the marked contents (empty = none)
    static QString getInnermostLanguage(const std::vector<QString>& languages)
    {
        for (auto it = languages.crbegin(); it != languages.crend(); ++it)
        {
            if (!it->isEmpty())
            {
                return *it;
            }
        }
        return QString();
    }
};

/// Text extractor for structure tree. Extracts sequences of structure items,
/// page sequences are stored in \p textSequences. They can be accessed using
/// getters.
class PDFStructureTreeTextExtractor
{
public:
    enum Option
    {
        None                = 0x0000,
        SkipArtifact        = 0x0001,   ///< Skip content marked as 'Artifact'
        AdjustReversedText  = 0x0002,   ///< Adjust reversed text
        CreateTreeMapping   = 0x0004,   ///< Create text mapping to structure tree item
        BoundingBoxes       = 0x0008,   ///< Compute bounding boxes of the texts
        KeepUnmarkedText    = 0x0010,   ///< Text outside of a marked content with MCID is a text of the sequence (not an unmatched text)
        MarkArtifacts       = 0x0020,   ///< The text of a marked content /Artifact (with or without MCID, ISO 32000-2 14.8.2.2) is a text of its own, marked as an artifact
        MarkLanguages       = 0x0040,   ///< The text of a marked content with /Lang in its property list (ISO 32000-2 14.9.2) is a text of its own with that language
    };
    Q_DECLARE_FLAGS(Options, Option)

    explicit PDFStructureTreeTextExtractor(const PDFDocument* document, const PDFStructureTree* tree, Options options);

    /// Sets the cancellation of perform, checked per page and per operator (optional)
    void setOperationControl(const PDFOperationControl* operationControl) { m_operationControl = operationControl; }

    /// Sets the progress of perform, a step per page (optional)
    void setProgress(PDFProgressSink* progress) { m_progress = progress; }

    /// Performs text extracting algorithm. Only \p pageIndices
    /// pages are processed for text extraction.
    /// \param pageIndices Page indices
    void perform(const std::vector<PDFInteger>& pageIndices);

    /// Returns a list of errors/warnings
    const QList<PDFRenderError>& getErrors() const { return m_errors; }

    /// Returns a list of unmatched text
    const QStringList& getUnmatchedText() const { return m_unmatchedText; }

    /// Returns text sequence for given page. If page number is invalid,
    /// then empty text sequence is returned.
    /// \param pageNumber Page number
    const PDFStructureTreeTextSequence& getTextSequence(PDFInteger pageNumber) const;

    struct TextItem
    {
        QRectF boundingRect;
        PDFInteger pageIndex = -1;
        QString text;
        std::vector<QRectF> characterBoundingRects;
        QString language;   ///< /Lang of its marked content (MarkLanguages), empty = the language of the element
    };

    using TextItems = std::vector<TextItem>;

    /// Returns text for given structure tree item. If structure tree item
    /// is not found, then empty list is returned. This functionality
    /// requires, that \p CreateTreeMapping flag is being set.
    /// \param item Item
    const TextItems& getText(const PDFStructureItem* item) const;

private:
    QList<PDFRenderError> m_errors;
    const PDFDocument* m_document;
    const PDFStructureTree* m_tree;
    const PDFOperationControl* m_operationControl = nullptr;
    PDFProgressSink* m_progress = nullptr;
    QStringList m_unmatchedText;
    std::map<PDFInteger, PDFStructureTreeTextSequence> m_textSequences;
    std::map<const PDFStructureItem*, TextItems> m_textForItems;
    Options m_options;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PDFStructureTreeTextExtractor::Options)

class PDFStructureTreeTextContentProcessor : public PDFPageContentProcessor
{
    using BaseClass = PDFPageContentProcessor;

public:
    explicit PDFStructureTreeTextContentProcessor(PDFRenderer::Features features,
                                                  const PDFPage* page,
                                                  const PDFDocument* document,
                                                  const PDFFontCache* fontCache,
                                                  const PDFICCProfileCache* iccProfileCache,
                                                  const PDFCMS* cms,
                                                  const PDFOptionalContentActivity* optionalContentActivity,
                                                  QTransform pagePointToDevicePointMatrix,
                                                  const PDFMeshQualitySettings& meshQualitySettings,
                                                  const PDFStructureTree* tree,
                                                  const std::map<PDFObjectReference, const PDFStructureItem*>* mapping,
                                                  PDFStructureTreeTextExtractor::Options extractorOptions) :
        BaseClass(page, document, fontCache, iccProfileCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
        m_features(features),
        m_tree(tree),
        m_mapping(mapping),
        m_extractorOptions(extractorOptions),
        m_pageIndex(document->getCatalog()->getPageIndexFromPageReference(page->getPageReference()))
    {

    }

    PDFStructureTreeTextSequence& takeSequence() { return m_textSequence; }
    QStringList& takeUnmatchedTexts() { return m_unmatchedText; }

    /// The page has been processed: the text after the last marked content is a text of the
    /// sequence (KeepUnmarkedText), also the text of a marked content, which the page has not closed
    void finishPage()
    {
        if (m_extractorOptions.testFlag(PDFStructureTreeTextExtractor::KeepUnmarkedText))
        {
            finishText();
        }
    }

protected:
    virtual bool isContentSuppressedByOC(PDFObjectReference ocgOrOcmd) override;
    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override;
    virtual void performMarkedContentBegin(const QByteArray& tag, const PDFObject& properties) override;
    virtual void performMarkedContentEnd() override;

private:
    const PDFStructureItem* getStructureTreeItemFromMCID(PDFInteger mcid) const;
    void finishText();

    bool isArtifact() const;
    bool isReversedText() const;

    struct MarkedContentInfo
    {
        QByteArray tag;
        PDFInteger mcid = -1;
        const PDFStructureItem* structureTreeItem = nullptr;
        bool isArtifact = false;
        bool isReversedText = false;
        QString language;   ///< /Lang of its property list (MarkLanguages)
    };

    /// The language of the current text: the innermost /Lang of the marked contents
    QString getCurrentLanguage() const;

    PDFRenderer::Features m_features;
    const PDFStructureTree* m_tree;
    const std::map<PDFObjectReference, const PDFStructureItem*>* m_mapping;
    std::vector<MarkedContentInfo> m_markedContentInfoStack;
    QString m_currentText;
    QRectF m_currentBoundingBox;
    PDFStructureTreeTextSequence m_textSequence;
    QStringList m_unmatchedText;
    PDFStructureTreeTextExtractor::Options m_extractorOptions;
    PDFInteger m_pageIndex;
    std::vector<QRectF> m_characterBoundingRects;
};

void PDFStructureTreeTextContentProcessor::finishText()
{
    QString trimmedText = m_currentText.trimmed();
    const int index = m_currentText.indexOf(trimmedText);
    Q_ASSERT(index != -1);
    if (trimmedText.size() < m_currentText.size())
    {
        // Fix character bounding boxes...
        if (m_characterBoundingRects.size() == static_cast<size_t>(m_currentText.size()))
        {
            std::vector<QRectF> boundingRects(std::next(m_characterBoundingRects.cbegin(), index), std::next(m_characterBoundingRects.cbegin(), index + trimmedText.length()));
            m_characterBoundingRects = std::move(boundingRects);
        }
        m_currentText = std::move(trimmedText);
    }

    const bool isArtifactText = isArtifact();
    if (!m_currentText.isEmpty() && (!m_extractorOptions.testFlag(PDFStructureTreeTextExtractor::SkipArtifact) || !isArtifactText))
    {
        if (m_extractorOptions.testFlag(PDFStructureTreeTextExtractor::AdjustReversedText) && isReversedText())
        {
            QString reversed;
            reversed.reserve(m_currentText.size());
            for (auto it = m_currentText.rbegin(); it != m_currentText.rend(); ++it)
            {
                reversed.push_back(*it);
            }
            m_currentText = qMove(reversed);
            std::reverse(m_characterBoundingRects.begin(), m_characterBoundingRects.end());
        }
        Q_ASSERT(static_cast<size_t>(m_currentText.size()) == m_characterBoundingRects.size() || m_characterBoundingRects.empty());
        m_textSequence.emplace_back(PDFStructureTreeTextItem::createText(std::move(m_currentText), m_pageIndex, m_currentBoundingBox, std::move(m_characterBoundingRects)));
        m_textSequence.back().isArtifact = isArtifactText && m_extractorOptions.testFlag(PDFStructureTreeTextExtractor::MarkArtifacts);
        if (m_extractorOptions.testFlag(PDFStructureTreeTextExtractor::MarkLanguages))
        {
            m_textSequence.back().language = getCurrentLanguage();
        }
    }
    m_currentText = QString();
    m_currentBoundingBox = QRectF();
    m_characterBoundingRects.clear();
}

bool PDFStructureTreeTextContentProcessor::isArtifact() const
{
    return std::any_of(m_markedContentInfoStack.cbegin(), m_markedContentInfoStack.cend(), [](const auto& item) { return item.isArtifact; });
}

bool PDFStructureTreeTextContentProcessor::isReversedText() const
{
    return std::any_of(m_markedContentInfoStack.cbegin(), m_markedContentInfoStack.cend(), [](const auto& item) { return item.isReversedText; });
}

QString PDFStructureTreeTextContentProcessor::getCurrentLanguage() const
{
    for (auto it = m_markedContentInfoStack.crbegin(); it != m_markedContentInfoStack.crend(); ++it)
    {
        if (!it->language.isEmpty())
        {
            return it->language;
        }
    }
    return QString();
}

void PDFStructureTreeTextContentProcessor::performMarkedContentBegin(const QByteArray& tag, const PDFObject& properties)
{
    MarkedContentInfo info;
    info.tag = tag;

    // A reading stream: the text of a marked content with /Lang (ISO 32000-2 14.9.2, the most
    // specific language of its text) is a text of its own with that language
    if (m_extractorOptions.testFlag(PDFStructureTreeTextExtractor::MarkLanguages))
    {
        info.language = PDFDocumentTextFlowHelper::getMarkedContentLanguage(getDocument(), properties, getPropertiesDictionary());
        if (!info.language.isEmpty())
        {
            finishText();
        }
    }

    // A reading stream: an artifact (ISO 32000-2 14.8.2.2) is a marked content /Artifact, mostly
    // without MCID (BMC or BDC with the type of the artifact): its text is a text of its own
    if (m_extractorOptions.testFlag(PDFStructureTreeTextExtractor::MarkArtifacts) && tag == "Artifact")
    {
        finishText();
        info.isArtifact = true;
    }

    if (properties.isDictionary())
    {
        const PDFDictionary* dictionary = properties.getDictionary();
        PDFObject mcid = dictionary->get("MCID");
        if (mcid.isInt())
        {
            // We must finish text, because we can have a sequence of text,
            // then subitem, then text, and followed by another subitem. They
            // can be interleaved.
            finishText();

            info.mcid = mcid.getInteger();
            info.structureTreeItem = getStructureTreeItemFromMCID(info.mcid);
            info.isArtifact = tag == "Artifact";
            info.isReversedText = tag == "ReversedChars";

            if (!info.structureTreeItem)
            {
                reportRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Structure tree item for MCID %1 not found.").arg(info.mcid));
            }

            if (info.structureTreeItem)
            {
                m_textSequence.emplace_back(PDFStructureTreeTextItem::createStartTag(info.structureTreeItem));
            }
        }
    }

    m_markedContentInfoStack.emplace_back(qMove(info));
}

void PDFStructureTreeTextContentProcessor::performMarkedContentEnd()
{
    if (m_markedContentInfoStack.empty())
    {
        // EMC without BMC / BDC (a hostile content stream; the processor reports it)
        return;
    }

    // The text of an artifact ends with it (MarkArtifacts), while it is still an artifact
    if (m_extractorOptions.testFlag(PDFStructureTreeTextExtractor::MarkArtifacts) && m_markedContentInfoStack.back().isArtifact)
    {
        finishText();
    }

    // The text of a marked content with /Lang ends with it, while it still has its language
    if (!m_markedContentInfoStack.back().language.isEmpty())
    {
        finishText();
    }

    MarkedContentInfo info = qMove(m_markedContentInfoStack.back());
    m_markedContentInfoStack.pop_back();

    if (info.mcid != -1)
    {
        finishText();
        if (info.structureTreeItem)
        {
            m_textSequence.emplace_back(PDFStructureTreeTextItem::createEndTag(info.structureTreeItem));
        }
    }

    // Check for text, which doesn't belong to any structure tree item
    if (m_markedContentInfoStack.empty() && m_extractorOptions.testFlag(PDFStructureTreeTextExtractor::KeepUnmarkedText))
    {
        finishText();
    }
    else if (m_markedContentInfoStack.empty())
    {
        m_currentText = m_currentText.trimmed();
        if (!m_currentText.isEmpty())
        {
            m_unmatchedText << qMove(m_currentText);
        }
        m_currentBoundingBox = QRectF();
        m_characterBoundingRects.clear();
    }
}

const PDFStructureItem* PDFStructureTreeTextContentProcessor::getStructureTreeItemFromMCID(PDFInteger mcid) const
{
    auto it = m_mapping->find(m_tree->getParent(getStructuralParentKey(), mcid));
    if (it != m_mapping->cend())
    {
        return it->second;
    }
    return nullptr;
}

bool PDFStructureTreeTextContentProcessor::isContentSuppressedByOC(PDFObjectReference ocgOrOcmd)
{
    if (m_features.testFlag(PDFRenderer::IgnoreOptionalContent))
    {
        return false;
    }

    return PDFPageContentProcessor::isContentSuppressedByOC(ocgOrOcmd);
}

bool PDFStructureTreeTextContentProcessor::isContentKindSuppressed(ContentKind kind) const
{
    switch (kind)
    {
        case ContentKind::Text:
        case ContentKind::Shapes:
        case ContentKind::Images:
        case ContentKind::Shading:
            return true;

        case ContentKind::Tiling:
            return false; // Tiling can have text

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    return false;
}

void PDFStructureTreeTextContentProcessor::performOutputCharacter(const PDFTextCharacterInfo& info)
{
    if (!isContentSuppressed())
    {
        if (!info.character.isNull() && info.character != QChar(QChar::SoftHyphen))
        {
            m_currentText.push_back(info.character);

            QPainterPath worldPath = info.outline ? info.outline->createPath(info.matrix) : QPainterPath();
            if (!worldPath.isEmpty())
            {
                QRectF boundingRect = worldPath.controlPointRect();
                m_currentBoundingBox = m_currentBoundingBox.united(boundingRect);
                m_characterBoundingRects.push_back(boundingRect);
            }
            else
            {
                m_characterBoundingRects.push_back(QRectF());
            }
        }
    }
}

PDFStructureTreeTextExtractor::PDFStructureTreeTextExtractor(const PDFDocument* document, const PDFStructureTree* tree, Options options) :
    m_document(document),
    m_tree(tree),
    m_options(options)
{

}

void PDFStructureTreeTextExtractor::perform(const std::vector<PDFInteger>& pageIndices)
{
    std::map<PDFObjectReference, const PDFStructureItem*> mapping;
    PDFStructureTreeReferenceCollector referenceCollector(&mapping);
    m_tree->accept(&referenceCollector);

    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;

    QMutex mutex;
    PDFCMSGeneric cms;
    PDFMeshQualitySettings mqs;
    PDFOptionalContentActivity oca(m_document, OCUsage::Export, nullptr);
    pdf::PDFModifiedDocument md(const_cast<PDFDocument*>(m_document), &oca);
    fontCache.setDocument(md);
    iccProfileCache.setDocument(md);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    auto generateTextLayout = [&, this](PDFInteger pageIndex)
    {
        const PDFCatalog* catalog = m_document->getCatalog();
        if (!catalog->getPage(pageIndex) || PDFOperationControl::isOperationCanceled(m_operationControl))
        {
            // Invalid page index
            return;
        }

        const PDFPage* page = catalog->getPage(pageIndex);
        Q_ASSERT(page);

        PDFStructureTreeTextContentProcessor processor(PDFRenderer::IgnoreOptionalContent, page, m_document, &fontCache, &iccProfileCache, &cms, &oca, QTransform(), mqs, m_tree, &mapping, m_options);
        processor.setOperationControl(m_operationControl);
        QList<PDFRenderError> errors = processor.processContents();
        processor.finishPage();

        QMutexLocker lock(&mutex);
        m_textSequences[pageIndex] = qMove(processor.takeSequence());
        m_unmatchedText << qMove(processor.takeUnmatchedTexts());
        m_errors.append(qMove(errors));
        lock.unlock();

        if (m_progress)
        {
            m_progress->step();
        }
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageIndices.begin(), pageIndices.end(), generateTextLayout);

    fontCache.setCacheShrinkEnabled(nullptr, true);

    if (m_options.testFlag(CreateTreeMapping))
    {
        for (const auto& sequence : m_textSequences)
        {
            std::stack<const PDFStructureItem*> stack;
            for (const PDFStructureTreeTextItem& sequenceItem : sequence.second)
            {
                switch (sequenceItem.type)
                {
                    case PDFStructureTreeTextItem::Type::StartTag:
                    {
                        stack.push(sequenceItem.item);
                        break;
                    }
                    case PDFStructureTreeTextItem::Type::EndTag:
                    {
                        stack.pop();
                        break;
                    }
                    case PDFStructureTreeTextItem::Type::Text:
                    {
                        if (!stack.empty())
                        {
                            m_textForItems[stack.top()].emplace_back(TextItem{ sequenceItem.boundingRect, sequenceItem.pageIndex, sequenceItem.text, sequenceItem.characterBoundingRects, sequenceItem.language });
                        }
                        break;
                    }

                    default:
                        break;
                }
            }
        }
    }
}

const PDFStructureTreeTextSequence& PDFStructureTreeTextExtractor::getTextSequence(PDFInteger pageIndex) const
{
    auto it = m_textSequences.find(pageIndex);
    if (it != m_textSequences.cend())
    {
        return it->second;
    }

    static PDFStructureTreeTextSequence dummy;
    return dummy;
}

const PDFStructureTreeTextExtractor::TextItems& PDFStructureTreeTextExtractor::getText(const PDFStructureItem* item) const
{
    auto it = m_textForItems.find(item);
    if (it != m_textForItems.cend())
    {
        return it->second;
    }

    static const TextItems dummy;
    return dummy;
}


/// The layout generator of a reading stream (PDFDocumentTextFlowFactory::setReadingStream, R4-13):
/// the characters of a marked content /Artifact (ISO 32000-2 14.8.2.2) go to a layout of their own,
/// so the artifacts (running heads, page numbers of the page) are special items by the marked
/// content of the page, never by a guess of their position
class PDFDocumentTextFlowLayoutGenerator : public PDFTextLayoutGenerator
{
    using BaseClass = PDFTextLayoutGenerator;

public:
    using BaseClass::BaseClass;

    /// Separates the artifacts and reads /Lang of the marked contents (a reading stream; off = the
    /// text layout generator as it is)
    void setSeparateArtifacts(bool isSeparate) { m_isSeparateArtifacts = isSeparate; }

    /// The layout of the text of the artifacts (call once, after the contents are processed)
    PDFTextLayout createArtifactLayout()
    {
        m_artifactLayout.perform();
        m_artifactLayout.optimize();
        return std::move(m_artifactLayout);
    }

    /// The language of a block of the text (ISO 32000-2 14.9.2): the /Lang of the marked content
    /// of the most of its characters, when more than half of its characters have one; empty = none
    QString getLanguage(const QRectF& boundingRect, const QString& text)
    {
        if (m_languagePoints.empty())
        {
            return QString();
        }

        // The points sorted by y once: a block looks only at the points of its band of y (the
        // blocks of a page lie mostly one below another), not at all points of the page
        if (!m_isLanguagePointsSorted)
        {
            std::stable_sort(m_languagePoints.begin(), m_languagePoints.end(), [](const auto& left, const auto& right) { return left.first.y() < right.first.y(); });
            m_isLanguagePointsSorted = true;
        }

        const QRectF rect = boundingRect.normalized();
        std::map<QString, qsizetype> counts;
        auto it = std::lower_bound(m_languagePoints.cbegin(), m_languagePoints.cend(), rect.top(), [](const auto& item, qreal y) { return item.first.y() < y; });
        for (; it != m_languagePoints.cend() && it->first.y() <= rect.bottom(); ++it)
        {
            if (rect.contains(it->first))
            {
                ++counts[it->second];
            }
        }

        const qsizetype characterCount = std::count_if(text.cbegin(), text.cend(), [](QChar character) { return !character.isSpace(); });
        QString result;
        qsizetype resultCount = 0;
        for (const auto& [language, count] : counts)
        {
            if (count > resultCount)
            {
                result = language;
                resultCount = count;
            }
        }
        return resultCount * 2 > characterCount ? result : QString();
    }

protected:
    virtual void performMarkedContentBegin(const QByteArray& tag, const PDFObject& properties) override
    {
        BaseClass::performMarkedContentBegin(tag, properties);
        m_artifactStack.push_back(tag == "Artifact");
        m_languageStack.push_back(m_isSeparateArtifacts ? PDFDocumentTextFlowHelper::getMarkedContentLanguage(getDocument(), properties, getPropertiesDictionary()) : QString());
    }

    virtual void performMarkedContentEnd() override
    {
        BaseClass::performMarkedContentEnd();
        if (!m_artifactStack.empty())
        {
            m_artifactStack.pop_back();
        }
        if (!m_languageStack.empty())
        {
            m_languageStack.pop_back();
        }
    }

    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override
    {
        const bool isArtifact = m_isSeparateArtifacts && std::find(m_artifactStack.cbegin(), m_artifactStack.cend(), true) != m_artifactStack.cend();
        if (!isArtifact)
        {
            BaseClass::performOutputCharacter(info);

            // The middle of the glyph (its advance, a third of the size of the font above the
            // base line) lies inside of the rectangle of the block of the character
            const QString language = PDFDocumentTextFlowHelper::getInnermostLanguage(m_languageStack);
            if (!language.isEmpty() && !isContentSuppressed() && !info.character.isSpace())
            {
                m_languagePoints.emplace_back(info.matrix.map(QPointF(info.advance * 0.5, info.fontSize * 0.3)), language);
                m_isLanguagePointsSorted = false;
            }
        }
        else if (!isContentSuppressed() && !info.character.isSpace())
        {
            m_artifactLayout.addCharacter(info);
        }
    }

private:
    bool m_isSeparateArtifacts = false;
    std::vector<bool> m_artifactStack;
    std::vector<QString> m_languageStack;
    std::vector<std::pair<QPointF, QString>> m_languagePoints;  ///< The characters of a marked content with /Lang and the language
    bool m_isLanguagePointsSorted = false;
    PDFTextLayout m_artifactLayout;
};

class PDFStructureTreeTextFlowCollector : public PDFStructureTreeAbstractVisitor
{
public:
    explicit PDFStructureTreeTextFlowCollector(PDFDocumentTextFlow::Items* items, const PDFStructureTreeTextExtractor* extractor) :
        m_items(items),
        m_extractor(extractor)
    {

    }

    /// The export of PDFDocumentTextFlowFactory::setStructureExport: the titles, and the
    /// texts of the items of the given pages only
    /// \param catalog Catalog of the document
    /// \param pageIndices Processed pages
    /// The item StructureTitle has its title (PDFDocumentTextFlowFactory::setStructureTitleText)
    void setTitleText(bool isTitleText)
    {
        m_isTitleText = isTitleText;
    }

    void setExport(const PDFCatalog* catalog, const std::vector<PDFInteger>& pageIndices)
    {
        m_catalog = catalog;
        m_isExport = true;
        m_pageIndices = std::set<PDFInteger>(pageIndices.cbegin(), pageIndices.cend());
    }

    /// The reading stream of PDFDocumentTextFlowFactory::setReadingStream (R4-13)
    /// \param catalog Catalog of the document (the pages of /Pg, /Lang of the document)
    void setReadingStream(const PDFCatalog* catalog)
    {
        m_isReadingStream = true;
        m_readingCatalog = catalog;
        m_languageStack.assign(1, catalog ? catalog->getLanguage() : QString());
    }

    virtual void visitStructureTree(const PDFStructureTree* structureTree) override;
    virtual void visitStructureElement(const PDFStructureElement* structureElement) override;
    virtual void visitStructureMarkedContentReference(const PDFStructureMarkedContentReference* structureMarkedContentReference) override;
    virtual void visitStructureObjectReference(const PDFStructureObjectReference* structureObjectReference) override;

private:
    void markHasContent();

    /// The item belongs to a processed page (its /Pg or the /Pg of a parent)
    bool isOnProcessedPage(const PDFStructureElement* structureElement) const;

    /// The texts of the element and of all of its descendants in the order of the tree. The tree
    /// is a tree (PDFStructureTree::parse reads every element once, with a limit of the depth),
    /// the depth is bounded here once more for a hostile tree.
    void collectTexts(const PDFStructureItem* item, int depth, PDFStructureTreeTextExtractor::TextItems* texts) const;

    /// The page of the element by its /Pg or of a parent (-1 = none)
    PDFInteger getElementPageIndex(const PDFStructureElement* structureElement) const;

    /// Adds an item of the reading stream with the language of the element, or with
    /// \p markedLanguage, the /Lang of the marked content of its text (14.9.2: the more specific)
    void addReadingItem(QRectF rect, PDFInteger pageIndex, QString text, PDFDocumentTextFlow::Flags flags, std::vector<QRectF> characterBoundingRects, const QString& markedLanguage = QString());

    /// The element of the reading stream (setReadingStream)
    void visitReadingElement(const PDFStructureElement* structureElement, bool isItemTextUsed);

    PDFDocumentTextFlow::Items* m_items;
    const PDFStructureTreeTextExtractor* m_extractor;
    std::vector<bool> m_hasContentStack;
    const PDFCatalog* m_catalog = nullptr;
    bool m_isExport = false;
    bool m_isTitleText = false;
    std::set<PDFInteger> m_pageIndices;
    bool m_isReadingStream = false;
    const PDFCatalog* m_readingCatalog = nullptr;
    QStringList m_languageStack;
};

void PDFStructureTreeTextFlowCollector::collectTexts(const PDFStructureItem* item, int depth, PDFStructureTreeTextExtractor::TextItems* texts) const
{
    constexpr int MAXIMUM_DEPTH = 1024;
    if (!item || depth > MAXIMUM_DEPTH)
    {
        return;
    }

    if (const PDFStructureElement* element = item->asStructureElement())
    {
        const PDFStructureTreeTextExtractor::TextItems& elementTexts = m_extractor->getText(element);
        texts->insert(texts->end(), elementTexts.cbegin(), elementTexts.cend());
    }

    for (size_t i = 0, count = item->getChildCount(); i < count; ++i)
    {
        collectTexts(item->getChild(i), depth + 1, texts);
    }
}

PDFInteger PDFStructureTreeTextFlowCollector::getElementPageIndex(const PDFStructureElement* structureElement) const
{
    constexpr int MAXIMUM_DEPTH = 1024;
    int depth = 0;
    for (const PDFStructureItem* item = structureElement; item && depth++ < MAXIMUM_DEPTH; item = item->getParent())
    {
        if (const PDFStructureElement* element = item->asStructureElement())
        {
            if (element->getPageReference().isValid() && m_readingCatalog)
            {
                const size_t pageIndex = m_readingCatalog->getPageIndexFromPageReference(element->getPageReference());
                return pageIndex != PDFCatalog::INVALID_PAGE_INDEX ? PDFInteger(pageIndex) : -1;
            }
        }
    }
    return -1;
}

void PDFStructureTreeTextFlowCollector::addReadingItem(QRectF rect, PDFInteger pageIndex, QString text, PDFDocumentTextFlow::Flags flags, std::vector<QRectF> characterBoundingRects, const QString& markedLanguage)
{
    markHasContent();
    PDFDocumentTextFlow::Item item{ rect, pageIndex, std::move(text), flags, std::move(characterBoundingRects) };
    item.language = !markedLanguage.isEmpty() ? markedLanguage : (m_languageStack.isEmpty() ? QString() : m_languageStack.back());
    m_items->push_back(std::move(item));
}

void PDFStructureTreeTextFlowCollector::visitReadingElement(const PDFStructureElement* structureElement, bool isItemTextUsed)
{
    using Flag = PDFDocumentTextFlow::Flag;

    // /Lang of the element, else of its parent (ISO 32000-2 14.9.2)
    const QString language = structureElement->getText(PDFStructureElement::Language);
    m_languageStack.push_back(language.isEmpty() && !m_languageStack.isEmpty() ? m_languageStack.back() : language);

    const QString title = isItemTextUsed ? structureElement->getText(PDFStructureElement::Title) : QString();
    const QString alternativeDescription = isItemTextUsed ? structureElement->getText(PDFStructureElement::AlternativeDescription) : QString();
    const QString expandedForm = isItemTextUsed ? structureElement->getText(PDFStructureElement::ExpandedForm) : QString();
    const QString actualText = isItemTextUsed ? structureElement->getText(PDFStructureElement::ActualText) : QString();

    if (!title.isEmpty())
    {
        addReadingItem(QRectF(), getElementPageIndex(structureElement), title, Flag::StructureTitle, { });
    }

    // The texts of the whole element only, when a replacement or a description needs them (the
    // walk over the descendants is not repeated for every element)
    PDFStructureTreeTextExtractor::TextItems texts;
    if (!actualText.isEmpty() || !expandedForm.isEmpty() || !alternativeDescription.isEmpty())
    {
        collectTexts(structureElement, 0, &texts);
    }

    // The rectangle and the page of the replacement: the text of the element on its first page
    PDFInteger pageIndex = getElementPageIndex(structureElement);
    QRectF rect;
    if (!texts.empty())
    {
        pageIndex = texts.front().pageIndex;
        for (const PDFStructureTreeTextExtractor::TextItem& text : texts)
        {
            if (text.pageIndex == pageIndex)
            {
                rect = rect.united(text.boundingRect);
            }
        }
    }

    // /ActualText replaces the content of the element and of its descendants (14.9.4), /E an
    // abbreviation (14.9.5); /ActualText wins, when both are given
    const bool hasActualText = !actualText.isEmpty();
    const bool hasExpandedForm = !hasActualText && !expandedForm.isEmpty();
    if (hasActualText || hasExpandedForm)
    {
        const PDFDocumentTextFlow::Flag kind = hasActualText ? Flag::StructureActualText : Flag::StructureExpandedForm;
        addReadingItem(rect, pageIndex, hasActualText ? actualText : expandedForm, kind, { });
        for (PDFStructureTreeTextExtractor::TextItem& text : texts)
        {
            addReadingItem(text.boundingRect, text.pageIndex, std::move(text.text), PDFDocumentTextFlow::Flags(Flag::Text) | Flag::ReplacedText | kind, std::move(text.characterBoundingRects), text.language);
        }

        // The descendants are replaced as a whole: their own titles and descriptions are not read
        m_languageStack.pop_back();
        return;
    }

    // /Alt is read for an element without any text (a figure, 14.9.3), else it is a special item
    if (!alternativeDescription.isEmpty())
    {
        addReadingItem(rect, pageIndex, alternativeDescription, texts.empty() ? PDFDocumentTextFlow::Flags(Flag::Text) | Flag::StructureAlternativeDescription : PDFDocumentTextFlow::Flags(Flag::StructureAlternativeDescription), { });
    }

    for (const PDFStructureTreeTextExtractor::TextItem& textItem : m_extractor->getText(structureElement))
    {
        addReadingItem(textItem.boundingRect, textItem.pageIndex, textItem.text, Flag::Text, textItem.characterBoundingRects, textItem.language);
    }

    acceptChildren(structureElement);
    m_languageStack.pop_back();
}

void PDFStructureTreeTextFlowCollector::visitStructureTree(const PDFStructureTree* structureTree)
{
    m_items->push_back(PDFDocumentTextFlow::Item{ QRectF(), -1, QString(), PDFDocumentTextFlow::StructureItemStart, {} });
    acceptChildren(structureTree);
    m_items->push_back(PDFDocumentTextFlow::Item{ QRectF(), -1, QString(), PDFDocumentTextFlow::StructureItemEnd, {} });
}

bool PDFStructureTreeTextFlowCollector::isOnProcessedPage(const PDFStructureElement* structureElement) const
{
    // /Pg is inherited from the parents (ISO 32000-2 14.7.2, Table 355); the depth is bounded
    // for a hostile tree
    constexpr int MAXIMUM_DEPTH = 1024;
    int depth = 0;
    const PDFStructureItem* item = structureElement;
    while (item && depth++ < MAXIMUM_DEPTH)
    {
        if (const PDFStructureElement* element = item->asStructureElement())
        {
            const PDFObjectReference pageReference = element->getPageReference();
            if (pageReference.isValid())
            {
                const size_t pageIndex = m_catalog ? m_catalog->getPageIndexFromPageReference(pageReference) : PDFCatalog::INVALID_PAGE_INDEX;
                return pageIndex != PDFCatalog::INVALID_PAGE_INDEX && m_pageIndices.count(PDFInteger(pageIndex));
            }
        }
        item = item->getParent();
    }

    // Without /Pg the page of the item is the page of its content items: a marked content
    // reference or an object reference has its own /Pg (Tables 357 and 358)
    bool hasPage = false;
    for (size_t i = 0, count = structureElement->getChildCount(); i < count; ++i)
    {
        const PDFStructureItem* child = structureElement->getChild(i);
        PDFObjectReference pageReference;
        if (const PDFStructureMarkedContentReference* markedContentReference = child->asStructureMarkedContentReference())
        {
            pageReference = markedContentReference->getPageReference();
        }
        else if (const PDFStructureObjectReference* objectReference = child->asStructureObjectReference())
        {
            pageReference = objectReference->getPageReference();
        }

        if (pageReference.isValid())
        {
            hasPage = true;
            const size_t pageIndex = m_catalog ? m_catalog->getPageIndexFromPageReference(pageReference) : PDFCatalog::INVALID_PAGE_INDEX;
            if (pageIndex != PDFCatalog::INVALID_PAGE_INDEX && m_pageIndices.count(PDFInteger(pageIndex)))
            {
                return true;
            }
        }
    }

    // An item without any page belongs to the whole document: it is in an export of all pages
    return !hasPage && m_catalog && m_pageIndices.size() >= m_catalog->getPageCount();
}

void PDFStructureTreeTextFlowCollector::markHasContent()
{
    for (size_t i = 0; i < m_hasContentStack.size(); ++i)
    {
        m_hasContentStack[i] = true;
    }
}

void PDFStructureTreeTextFlowCollector::visitStructureElement(const PDFStructureElement* structureElement)
{
    size_t index = m_items->size();
    m_items->push_back(PDFDocumentTextFlow::Item{ QRectF(), -1, QString(), PDFDocumentTextFlow::StructureItemStart, {} });

    // Mark stack so we can delete unused items
    m_hasContentStack.push_back(false);

    // An export takes the texts of the item only for its pages (an item of another page would
    // bring its description into the text of the selected pages)
    const bool isItemTextUsed = !m_isExport || isOnProcessedPage(structureElement);

    if (m_isReadingStream)
    {
        visitReadingElement(structureElement, isItemTextUsed);

        const bool hasReadingContent = m_hasContentStack.back();
        m_hasContentStack.pop_back();
        m_items->push_back(PDFDocumentTextFlow::Item{ QRectF(), -1, QString(), PDFDocumentTextFlow::StructureItemEnd, {} });
        if (!hasReadingContent)
        {
            m_items->erase(std::next(m_items->begin(), index), m_items->end());
        }
        return;
    }

    QString title = isItemTextUsed ? structureElement->getText(PDFStructureElement::Title) : QString();
    QString language = isItemTextUsed ? structureElement->getText(PDFStructureElement::Language) : QString();
    QString alternativeDescription = isItemTextUsed ? structureElement->getText(PDFStructureElement::AlternativeDescription) : QString();
    QString expandedForm = isItemTextUsed ? structureElement->getText(PDFStructureElement::ExpandedForm) : QString();
    QString actualText = isItemTextUsed ? structureElement->getText(PDFStructureElement::ActualText) : QString();
    QString phoneme = isItemTextUsed ? structureElement->getText(PDFStructureElement::Phoneme) : QString();

    if (!title.isEmpty())
    {
        markHasContent();

        // Without setStructureTitleText the item is empty, as the older versions gave it (the audio book)
        m_items->push_back(PDFDocumentTextFlow::Item{ QRectF(), -1, m_isTitleText ? title : QString(), PDFDocumentTextFlow::StructureTitle, {} });
    }

    if (!language.isEmpty())
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{ QRectF(), -1, language, PDFDocumentTextFlow::StructureLanguage, {} });
    }

    if (!alternativeDescription.isEmpty())
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{ QRectF(), -1, alternativeDescription, PDFDocumentTextFlow::StructureAlternativeDescription, {} });
    }

    if (!expandedForm.isEmpty())
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{ QRectF(), -1, expandedForm, PDFDocumentTextFlow::StructureExpandedForm, {} });
    }

    if (!actualText.isEmpty())
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{ QRectF(), -1, actualText, PDFDocumentTextFlow::StructureActualText, {} });
    }

    if (!phoneme.isEmpty())
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{ QRectF(), -1, phoneme, PDFDocumentTextFlow::StructurePhoneme, {} });
    }

    for (const auto& textItem : m_extractor->getText(structureElement))
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{ textItem.boundingRect, textItem.pageIndex, textItem.text, PDFDocumentTextFlow::Text, textItem.characterBoundingRects });
    }

    acceptChildren(structureElement);

    const bool hasContent = m_hasContentStack.back();
    m_hasContentStack.pop_back();

    m_items->push_back(PDFDocumentTextFlow::Item{ QRectF(), -1, QString(), PDFDocumentTextFlow::StructureItemEnd, {} });

    if (!hasContent)
    {
        // Delete unused content
        m_items->erase(std::next(m_items->begin(), index), m_items->end());
    }
}

void PDFStructureTreeTextFlowCollector::visitStructureMarkedContentReference(const PDFStructureMarkedContentReference* structureMarkedContentReference)
{
    acceptChildren(structureMarkedContentReference);
}

void PDFStructureTreeTextFlowCollector::visitStructureObjectReference(const PDFStructureObjectReference* structureObjectReference)
{
    acceptChildren(structureObjectReference);
}

PDFDocumentTextFlow PDFDocumentTextFlowFactory::create(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, Algorithm algorithm)
{
    PDFDocumentTextFlow result;
    PDFStructureTree structureTree;

    const PDFCatalog* catalog = document->getCatalog();
    if (algorithm != Algorithm::Layout)
    {
        structureTree = PDFStructureTree::parse(&document->getStorage(), catalog->getStructureTreeRoot());
    }

    if (algorithm == Algorithm::Auto)
    {
        // Determine algorithm
        if (catalog->isLogicalStructureMarked() && structureTree.isValid())
        {
            algorithm = Algorithm::Structure;
        }
        else
        {
            algorithm = Algorithm::Layout;
        }
    }

    Q_ASSERT(algorithm != Algorithm::Auto);

    if (m_progress && !pageIndices.empty())
    {
        m_progress->start(pageIndices.size(), ProgressStartupInfo());
    }

    // Perform algorithm to retrieve document text
    switch (algorithm)
    {
        case Algorithm::Layout:
        {
            PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
            PDFICCProfileCache iccProfileCache;

            std::map<PDFInteger, PDFDocumentTextFlow::Items> items;

            QMutex mutex;
            PDFCMSGeneric cms;
            PDFMeshQualitySettings mqs;
            PDFOptionalContentActivity oca(document, OCUsage::Export, nullptr);
            pdf::PDFModifiedDocument md(const_cast<PDFDocument*>(document), &oca);
            fontCache.setDocument(md);
            iccProfileCache.setDocument(md);
            fontCache.setCacheShrinkEnabled(nullptr, false);

            auto generateTextLayout = [this, &items, &mutex, &fontCache, &iccProfileCache, &cms, &mqs, &oca, document, catalog](PDFInteger pageIndex)
            {
                if (!catalog->getPage(pageIndex) || PDFOperationControl::isOperationCanceled(m_operationControl))
                {
                    // Invalid page index
                    return;
                }

                const PDFPage* page = catalog->getPage(pageIndex);
                Q_ASSERT(page);

                PDFDocumentTextFlowLayoutGenerator generator(PDFRenderer::IgnoreOptionalContent, page, document, &fontCache, &iccProfileCache, &cms, &oca, QTransform(), mqs);
                generator.setOperationControl(m_operationControl);
                generator.setSeparateArtifacts(m_isReadingStream);
                QList<PDFRenderError> errors = generator.processContents();
                PDFTextLayout textLayout = generator.createTextLayout();
                const PDFTextFlow::FlowFlags flowFlags = PDFTextFlow::FlowFlags(PDFTextFlow::SeparateBlocks) | PDFTextFlow::RemoveSoftHyphen;
                PDFTextFlows textFlows = PDFTextFlow::createTextFlows(textLayout, flowFlags, pageIndex);

                PDFDocumentTextFlow::Items flowItems;
                flowItems.emplace_back(PDFDocumentTextFlow::Item{ QRectF(), pageIndex, PDFTranslationContext::tr("Page %1").arg(pageIndex + 1), PDFDocumentTextFlow::PageStart, {} });
                for (const PDFTextFlow& textFlow : textFlows)
                {
                    flowItems.emplace_back(PDFDocumentTextFlow::Item{ textFlow.getBoundingBox(), pageIndex, textFlow.getText(), PDFDocumentTextFlow::Text, textFlow.getBoundingBoxes() });
                    if (m_isReadingStream)
                    {
                        // 14.9.2: /Lang of the marked content of the text
                        flowItems.back().language = generator.getLanguage(flowItems.back().boundingRect, flowItems.back().text);
                    }
                }

                if (m_isReadingStream)
                {
                    // The artifacts in the place of the page, where they are drawn: before the
                    // first text, whose top lies below their top (the page has y upwards)
                    PDFTextLayout artifactLayout = generator.createArtifactLayout();
                    for (const PDFTextFlow& artifactFlow : PDFTextFlow::createTextFlows(artifactLayout, flowFlags, pageIndex))
                    {
                        const QRectF artifactRect = artifactFlow.getBoundingBox();
                        const qreal artifactTop = artifactRect.normalized().bottom();
                        auto it = std::find_if(std::next(flowItems.begin()), flowItems.end(), [artifactTop](const PDFDocumentTextFlow::Item& item) { return item.boundingRect.normalized().bottom() < artifactTop; });
                        flowItems.insert(it, PDFDocumentTextFlow::Item{ artifactRect, pageIndex, artifactFlow.getText(), PDFDocumentTextFlow::Artifact, artifactFlow.getBoundingBoxes() });
                    }
                }
                flowItems.emplace_back(PDFDocumentTextFlow::Item{ QRectF(), pageIndex, QString(), PDFDocumentTextFlow::PageEnd, {} });

                QMutexLocker lock(&mutex);
                items[pageIndex] = qMove(flowItems);
                m_errors.append(qMove(errors));
                lock.unlock();

                if (m_progress)
                {
                    m_progress->step();
                }
            };

            PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageIndices.begin(), pageIndices.end(), generateTextLayout);

            fontCache.setCacheShrinkEnabled(nullptr, true);

            PDFDocumentTextFlow::Items flowItems;
            for (const auto& item : items)
            {
                flowItems.insert(flowItems.end(), std::make_move_iterator(item.second.begin()), std::make_move_iterator(item.second.end()));
            }

            result = PDFDocumentTextFlow(qMove(flowItems));
            break;
        }

        case Algorithm::Structure:
        {
            if (!structureTree.isValid())
            {
                m_errors << PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Valid tagged document required."));
                break;
            }

            PDFStructureTreeTextExtractor::Options options = PDFStructureTreeTextExtractor::SkipArtifact | PDFStructureTreeTextExtractor::AdjustReversedText | PDFStructureTreeTextExtractor::CreateTreeMapping;
            options.setFlag(PDFStructureTreeTextExtractor::BoundingBoxes, m_calculateBoundingBoxes);
            options.setFlag(PDFStructureTreeTextExtractor::MarkLanguages, m_isReadingStream);
            PDFStructureTreeTextExtractor extractor(document, &structureTree, options);
            extractor.setOperationControl(m_operationControl);
            extractor.setProgress(m_progress);
            extractor.perform(pageIndices);

            PDFDocumentTextFlow::Items flowItems;
            PDFStructureTreeTextFlowCollector collector(&flowItems, &extractor);
            collector.setTitleText(m_isStructureTitleText);
            if (m_isStructureExport || m_isReadingStream)
            {
                // A reading stream of some pages never reads a title, a description or a
                // replacement of an element of another page (the audio book of the pages)
                collector.setExport(catalog, pageIndices);
            }
            if (m_isReadingStream)
            {
                collector.setReadingStream(catalog);
            }
            structureTree.accept(&collector);

            if (m_isReadingStream)
            {
                // B76: the marks of the pages as in Layout and Content (PdfTool --say-page-numbers,
                // readSpecial page-numbers)
                flowItems = PDFDocumentTextFlowHelper::insertPageMarks(std::move(flowItems));
            }

            result = PDFDocumentTextFlow(qMove(flowItems));
            m_errors.append(extractor.getErrors());
            break;
        }

        case Algorithm::Content:
        {
            PDFStructureTreeTextExtractor::Options options = PDFStructureTreeTextExtractor::None;
            options.setFlag(PDFStructureTreeTextExtractor::BoundingBoxes, m_calculateBoundingBoxes);

            // The order of the content stream is all of its text: also the text, which is not in
            // a marked content of the structure (an untagged page has only such text)
            options.setFlag(PDFStructureTreeTextExtractor::KeepUnmarkedText, m_isUnmarkedText);
            options.setFlag(PDFStructureTreeTextExtractor::MarkArtifacts, m_isReadingStream);
            options.setFlag(PDFStructureTreeTextExtractor::MarkLanguages, m_isReadingStream);
            PDFStructureTreeTextExtractor extractor(document, &structureTree, options);
            extractor.setOperationControl(m_operationControl);
            extractor.setProgress(m_progress);
            extractor.perform(pageIndices);

            PDFDocumentTextFlow::Items flowItems;
            for (PDFInteger pageIndex : pageIndices)
            {
                flowItems.emplace_back(PDFDocumentTextFlow::Item{ QRectF(), pageIndex, PDFTranslationContext::tr("Page %1").arg(pageIndex + 1), PDFDocumentTextFlow::PageStart, {} });
                for (const PDFStructureTreeTextItem& sequenceItem : extractor.getTextSequence(pageIndex))
                {
                    if (sequenceItem.type == PDFStructureTreeTextItem::Type::Text)
                    {
                        const PDFDocumentTextFlow::Flag flag = sequenceItem.isArtifact ? PDFDocumentTextFlow::Artifact : PDFDocumentTextFlow::Text;
                        flowItems.emplace_back(PDFDocumentTextFlow::Item{ sequenceItem.boundingRect, pageIndex, sequenceItem.text, flag, sequenceItem.characterBoundingRects });
                        flowItems.back().language = sequenceItem.language;
                    }
                }
                flowItems.emplace_back(PDFDocumentTextFlow::Item{ QRectF(), pageIndex, QString(), PDFDocumentTextFlow::PageEnd, {} });
            }

            result = PDFDocumentTextFlow(qMove(flowItems));
            m_errors.append(extractor.getErrors());
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    if (m_progress && !pageIndices.empty())
    {
        m_progress->finish();
    }

    if (m_isReadingStream && !catalog->getLanguage().isEmpty())
    {
        // Layout and Content know only the language of the document (14.9.2); the structure has
        // set the language of its elements already
        PDFDocumentTextFlow::Items items = result.getItems();
        for (PDFDocumentTextFlow::Item& item : items)
        {
            if (item.language.isEmpty())
            {
                item.language = catalog->getLanguage();
            }
        }
        result = PDFDocumentTextFlow(std::move(items));
    }

    return result;
}

PDFDocumentTextFlowFactory::Algorithm PDFDocumentTextFlowFactory::getEffectiveAlgorithm(const PDFDocument* document, Algorithm algorithm)
{
    if (algorithm != Algorithm::Auto)
    {
        return algorithm;
    }

    // The rule of create
    return document->getCatalog()->isLogicalStructureMarked() && hasValidStructureTree(document) ? Algorithm::Structure : Algorithm::Layout;
}

bool PDFDocumentTextFlowFactory::hasValidStructureTree(const PDFDocument* document)
{
    return PDFStructureTree::parse(&document->getStorage(), document->getCatalog()->getStructureTreeRoot()).isValid();
}

PDFDocumentTextFlow PDFDocumentTextFlowFactory::create(const PDFDocument* document, Algorithm algorithm)
{
    std::vector<pdf::PDFInteger> pageIndices;
    pageIndices.resize(document->getCatalog()->getPageCount(), 0);
    std::iota(pageIndices.begin(), pageIndices.end(), 0);

    return create(document, pageIndices, algorithm);
}

void PDFDocumentTextFlowFactory::setCalculateBoundingBoxes(bool calculateBoundingBoxes)
{
    m_calculateBoundingBoxes = calculateBoundingBoxes;
}

void PDFDocumentTextFlowEditor::setTextFlow(PDFDocumentTextFlow textFlow)
{
    m_originalTextFlow = std::make_shared<const PDFDocumentTextFlow>(std::move(textFlow));
    createEditedFromOriginalTextFlow();
}

void PDFDocumentTextFlowEditor::setSelectionActive(bool active)
{
    for (auto& item : m_editedTextFlow)
    {
        if (item.editedItemFlags.testFlag(Selected))
        {
            item.editedItemFlags.setFlag(Removed, !active);
        }
    }
}

void PDFDocumentTextFlowEditor::select(size_t index, bool select)
{
    getEditedItem(index)->editedItemFlags.setFlag(Selected, select);
}

void PDFDocumentTextFlowEditor::deselect()
{
    for (auto& item : m_editedTextFlow)
    {
        item.editedItemFlags.setFlag(Selected, false);
    }
}

void PDFDocumentTextFlowEditor::removeItem(size_t index)
{
    getEditedItem(index)->editedItemFlags.setFlag(Removed, true);
}

void PDFDocumentTextFlowEditor::addItem(size_t index)
{
    getEditedItem(index)->editedItemFlags.setFlag(Removed, false);
}

void PDFDocumentTextFlowEditor::clear()
{
    m_originalTextFlow.reset();
    m_editedTextFlow.clear();
    m_pageIndicesMapping.clear();
}

void PDFDocumentTextFlowEditor::setText(const QString& text, size_t index)
{
    EditedItem* item = getEditedItem(index);
    item->text = text;
    updateModifiedFlag(index);
}

bool PDFDocumentTextFlowEditor::isSelectionEmpty() const
{
    return std::all_of(m_editedTextFlow.cbegin(), m_editedTextFlow.cend(), [](const auto& item) { return !item.editedItemFlags.testFlag(Selected); });
}

bool PDFDocumentTextFlowEditor::isSelectionModified() const
{
    return std::any_of(m_editedTextFlow.cbegin(), m_editedTextFlow.cend(), [](const auto& item) { return item.editedItemFlags.testFlag(Selected) && item.editedItemFlags.testFlag(Modified); });
}

void PDFDocumentTextFlowEditor::selectByRectangle(QRectF rectangle)
{
    for (auto& item : m_editedTextFlow)
    {
        const QRectF& boundingRectangle = item.boundingRect;

        if (boundingRectangle.isEmpty())
        {
            item.editedItemFlags.setFlag(Selected, false);
            continue;
        }

        const bool isContained = rectangle.contains(boundingRectangle);
        item.editedItemFlags.setFlag(Selected, isContained);
    }
}

void PDFDocumentTextFlowEditor::selectByContainedText(QString text)
{
    for (auto& item : m_editedTextFlow)
    {
        const bool isContained = item.text.contains(text, Qt::CaseSensitive);
        item.editedItemFlags.setFlag(Selected, isContained);
    }
}

void PDFDocumentTextFlowEditor::selectByRegularExpression(const QRegularExpression& expression)
{
    for (auto& item : m_editedTextFlow)
    {
        QRegularExpressionMatch match = expression.match(item.text, 0, QRegularExpression::NormalMatch, QRegularExpression::NoMatchOption);
        const bool hasMatch = match.hasMatch();
        item.editedItemFlags.setFlag(Selected, hasMatch);
    }
}

void PDFDocumentTextFlowEditor::selectByPageIndices(const pdf::PDFClosedIntervalSet& indices)
{
    std::vector<PDFInteger> pageIndices = indices.unfold();
    std::sort(pageIndices.begin(), pageIndices.end());

    for (auto& item : m_editedTextFlow)
    {
        const bool isPageValid = std::binary_search(pageIndices.begin(), pageIndices.end(), item.pageIndex + 1);
        item.editedItemFlags.setFlag(Selected, isPageValid);
    }
}

void PDFDocumentTextFlowEditor::restoreOriginalTexts()
{
    for (auto& item : m_editedTextFlow)
    {
        if (item.editedItemFlags.testFlag(Selected))
        {
            item.text = getOriginalItem(item.originalIndex)->text;
            item.editedItemFlags.setFlag(Modified, false);
        }
    }
}

void PDFDocumentTextFlowEditor::moveSelectionUp()
{
    size_t originalSize = m_editedTextFlow.size();
    size_t firstSelectedIndex = originalSize;

    EditedItems selectedItems;
    for (auto it = m_editedTextFlow.begin(); it != m_editedTextFlow.end();)
    {
        if (it->editedItemFlags.testFlag(Selected))
        {
            if (firstSelectedIndex == originalSize)
            {
                firstSelectedIndex = std::distance(m_editedTextFlow.begin(), it);
            }

            selectedItems.emplace_back(std::move(*it));
            it = m_editedTextFlow.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (firstSelectedIndex > 0 && firstSelectedIndex != originalSize)
    {
        --firstSelectedIndex;
    }
    firstSelectedIndex = qMin(firstSelectedIndex, m_editedTextFlow.size());

    m_editedTextFlow.insert(std::next(m_editedTextFlow.begin(), firstSelectedIndex), std::make_move_iterator(selectedItems.begin()), std::make_move_iterator(selectedItems.end()));

    // B74: the items of the pages have new indices
    createPageMapping();
}

void PDFDocumentTextFlowEditor::moveSelectionDown()
{
    size_t originalSize = m_editedTextFlow.size();
    size_t lastSelectedIndex = originalSize;

    EditedItems selectedItems;
    for (auto it = m_editedTextFlow.begin(); it != m_editedTextFlow.end();)
    {
        if (it->editedItemFlags.testFlag(Selected))
        {
            lastSelectedIndex = std::distance(m_editedTextFlow.begin(), it);
            selectedItems.emplace_back(std::move(*it));
            it = m_editedTextFlow.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (lastSelectedIndex < m_editedTextFlow.size())
    {
        ++lastSelectedIndex;
    }
    lastSelectedIndex = qMin(lastSelectedIndex, m_editedTextFlow.size());

    m_editedTextFlow.insert(std::next(m_editedTextFlow.begin(), lastSelectedIndex), std::make_move_iterator(selectedItems.begin()), std::make_move_iterator(selectedItems.end()));

    // B74: the items of the pages have new indices
    createPageMapping();
}

PDFDocumentTextFlowEditor::PageIndicesMappingRange PDFDocumentTextFlowEditor::getItemsForPageIndex(PDFInteger pageIndex) const
{
    auto comparator = [](const auto& l, const auto& r)
    {
        return l.first < r.first;
    };
    return std::equal_range(m_pageIndicesMapping.cbegin(), m_pageIndicesMapping.cend(), std::make_pair(pageIndex, size_t(0)), comparator);
}

PDFDocumentTextFlow PDFDocumentTextFlowEditor::createEditedTextFlow() const
{
    PDFDocumentTextFlow::Items items;
    items.reserve(getItemCount());

    const size_t size = getItemCount();
    for (size_t i = 0; i < size; ++i)
    {
        if (isRemoved(i))
        {
            continue;
        }

        // B73: the original item of an edited item is the item of its originalIndex (the empty
        // items are left out of the edited flow and the items can be moved)
        PDFDocumentTextFlow::Item item = *getOriginalItem(getEditedItem(i)->originalIndex);
        item.text = getText(i);
        items.emplace_back(std::move(item));
    }

    return PDFDocumentTextFlow(std::move(items));
}

void PDFDocumentTextFlowEditor::createPageMapping()
{
    m_pageIndicesMapping.clear();
    m_pageIndicesMapping.reserve(m_editedTextFlow.size());

    for (size_t i = 0; i < m_editedTextFlow.size(); ++i)
    {
        m_pageIndicesMapping.emplace_back(m_editedTextFlow[i].pageIndex, i);
    }

    std::sort(m_pageIndicesMapping.begin(), m_pageIndicesMapping.end());
}

void PDFDocumentTextFlowEditor::createEditedFromOriginalTextFlow()
{
    // A new flow replaces the items of the last one (setTextFlow called again)
    const size_t count = m_originalTextFlow ? m_originalTextFlow->getSize() : 0;
    m_editedTextFlow.clear();
    m_editedTextFlow.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        const PDFDocumentTextFlow::Item* originalItem = getOriginalItem(i);

        if (originalItem->text.trimmed().isEmpty())
        {
            continue;
        }

        EditedItem editedItem;
        static_cast<PDFDocumentTextFlow::Item&>(editedItem) = *originalItem;
        editedItem.originalIndex = i;
        editedItem.editedItemFlags = originalItem->isText() ? None : Removed;
        m_editedTextFlow.emplace_back(std::move(editedItem));
    }

    createPageMapping();
}

void PDFDocumentTextFlowEditor::updateModifiedFlag(size_t index)
{
    // B73: compared with the original item of the edited item, not with the item of its index
    const bool isModified = getText(index) != getOriginalItem(getEditedItem(index)->originalIndex)->text;

    EditedItem* item = getEditedItem(index);
    item->editedItemFlags.setFlag(Modified, isModified);
}

std::map<PDFInteger, PDFDocumentTextFlow> PDFDocumentTextFlow::split(Flags mask) const
{
    std::map<PDFInteger, PDFDocumentTextFlow> result;

    for (const Item& item : m_items)
    {
        if (item.flags & mask)
        {
            result[item.pageIndex].addItem(item);
        }
    }

    return result;
}

void PDFDocumentTextFlow::append(const PDFDocumentTextFlow& textFlow)
{
    m_items.insert(m_items.end(), textFlow.m_items.cbegin(), textFlow.m_items.cend());
}

QString PDFDocumentTextFlow::getText() const
{
    QStringList texts;

    for (const auto& item : m_items)
    {
        texts << item.text.trimmed();
    }

    return texts.join(" ");
}

}   // namespace pdf
