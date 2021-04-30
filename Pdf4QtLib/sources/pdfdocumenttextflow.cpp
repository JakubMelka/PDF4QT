//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfdocumenttextflow.h"
#include "pdfdocument.h"
#include "pdfstructuretree.h"
#include "pdfcompiler.h"
#include "pdfexecutionpolicy.h"
#include "pdfconstants.h"
#include "pdfcms.h"

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
    PDFStructureTreeTextItem(Type type, const PDFStructureItem* item, QString text) :
        type(type), item(item), text(qMove(text))
    {

    }

    static PDFStructureTreeTextItem createText(QString text) { return PDFStructureTreeTextItem(Type::Text, nullptr, qMove(text)); }
    static PDFStructureTreeTextItem createStartTag(const PDFStructureItem* item) { return PDFStructureTreeTextItem(Type::StartTag, item, QString()); }
    static PDFStructureTreeTextItem createEndTag(const PDFStructureItem* item) { return PDFStructureTreeTextItem(Type::EndTag, item, QString()); }

    Type type = Type::Text;
    const PDFStructureItem* item = nullptr;
    QString text;
};

using PDFStructureTreeTextSequence = std::vector<PDFStructureTreeTextItem>;

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
    };
    Q_DECLARE_FLAGS(Options, Option)

    explicit PDFStructureTreeTextExtractor(const PDFDocument* document, const PDFStructureTree* tree, Options options);

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

    /// Returns text for given structure tree item. If structure tree item
    /// is not found, then empty list is returned. This functionality
    /// requires, that \p CreateTreeMapping flag is being set.
    /// \param item Item
    const QStringList& getText(const PDFStructureItem* item) const;

private:
    QList<PDFRenderError> m_errors;
    const PDFDocument* m_document;
    const PDFStructureTree* m_tree;
    QStringList m_unmatchedText;
    std::map<PDFInteger, PDFStructureTreeTextSequence> m_textSequences;
    std::map<const PDFStructureItem*, QStringList> m_textForItems;
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
                                                  const PDFCMS* cms,
                                                  const PDFOptionalContentActivity* optionalContentActivity,
                                                  QMatrix pagePointToDevicePointMatrix,
                                                  const PDFMeshQualitySettings& meshQualitySettings,
                                                  const PDFStructureTree* tree,
                                                  const std::map<PDFObjectReference, const PDFStructureItem*>* mapping,
                                                  PDFStructureTreeTextExtractor::Options extractorOptions) :
        BaseClass(page, document, fontCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
        m_features(features),
        m_tree(tree),
        m_mapping(mapping),
        m_extractorOptions(extractorOptions)
    {

    }

    PDFStructureTreeTextSequence& takeSequence() { return m_textSequence; }
    QStringList& takeUnmatchedTexts() { return m_unmatchedText; }

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
    };

    PDFRenderer::Features m_features;
    const PDFStructureTree* m_tree;
    const std::map<PDFObjectReference, const PDFStructureItem*>* m_mapping;
    std::vector<MarkedContentInfo> m_markedContentInfoStack;
    QString m_currentText;
    PDFStructureTreeTextSequence m_textSequence;
    QStringList m_unmatchedText;
    PDFStructureTreeTextExtractor::Options m_extractorOptions;
};

void PDFStructureTreeTextContentProcessor::finishText()
{
    m_currentText = m_currentText.trimmed();
    if (!m_currentText.isEmpty() && (!m_extractorOptions.testFlag(PDFStructureTreeTextExtractor::SkipArtifact) || !isArtifact()))
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
        }
        m_textSequence.emplace_back(PDFStructureTreeTextItem::createText(qMove(m_currentText)));
    }
    m_currentText = QString();
}

bool PDFStructureTreeTextContentProcessor::isArtifact() const
{
    return std::any_of(m_markedContentInfoStack.cbegin(), m_markedContentInfoStack.cend(), [](const auto& item) { return item.isArtifact; });
}

bool PDFStructureTreeTextContentProcessor::isReversedText() const
{
    return std::any_of(m_markedContentInfoStack.cbegin(), m_markedContentInfoStack.cend(), [](const auto& item) { return item.isReversedText; });
}

void PDFStructureTreeTextContentProcessor::performMarkedContentBegin(const QByteArray& tag, const PDFObject& properties)
{
    MarkedContentInfo info;
    info.tag = tag;

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
    if (m_markedContentInfoStack.empty())
    {
        m_currentText = m_currentText.trimmed();
        if (!m_currentText.isEmpty())
        {
            m_unmatchedText << qMove(m_currentText);
        }
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
        case ContentKind::Shapes:
        case ContentKind::Text:
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

    QMutex mutex;
    PDFCMSGeneric cms;
    PDFMeshQualitySettings mqs;
    PDFOptionalContentActivity oca(m_document, OCUsage::Export, nullptr);
    pdf::PDFModifiedDocument md(const_cast<PDFDocument*>(m_document), &oca);
    fontCache.setDocument(md);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    auto generateTextLayout = [&, this](PDFInteger pageIndex)
    {
        const PDFCatalog* catalog = m_document->getCatalog();
        if (!catalog->getPage(pageIndex))
        {
            // Invalid page index
            return;
        }

        const PDFPage* page = catalog->getPage(pageIndex);
        Q_ASSERT(page);

        PDFStructureTreeTextContentProcessor processor(PDFRenderer::IgnoreOptionalContent, page, m_document, &fontCache, &cms, &oca, QMatrix(), mqs, m_tree, &mapping, m_options);
        QList<PDFRenderError> errors = processor.processContents();

        QMutexLocker lock(&mutex);
        m_textSequences[pageIndex] = qMove(processor.takeSequence());
        m_unmatchedText << qMove(processor.takeUnmatchedTexts());
        m_errors.append(qMove(errors));
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
                        stack.push(sequenceItem.item);
                        break;
                    case PDFStructureTreeTextItem::Type::EndTag:
                        stack.pop();
                        break;
                    case PDFStructureTreeTextItem::Type::Text:
                        if (!stack.empty())
                        {
                            m_textForItems[stack.top()] << sequenceItem.text;
                        }
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

const QStringList& PDFStructureTreeTextExtractor::getText(const PDFStructureItem* item) const
{
    auto it = m_textForItems.find(item);
    if (it != m_textForItems.cend())
    {
        return it->second;
    }

    static const QStringList dummy;
    return dummy;
}


class PDFStructureTreeTextFlowCollector : public PDFStructureTreeAbstractVisitor
{
public:
    explicit PDFStructureTreeTextFlowCollector(PDFDocumentTextFlow::Items* items, const PDFStructureTreeTextExtractor* extractor) :
        m_items(items),
        m_extractor(extractor)
    {

    }

    virtual void visitStructureTree(const PDFStructureTree* structureTree) override;
    virtual void visitStructureElement(const PDFStructureElement* structureElement) override;
    virtual void visitStructureMarkedContentReference(const PDFStructureMarkedContentReference* structureMarkedContentReference) override;
    virtual void visitStructureObjectReference(const PDFStructureObjectReference* structureObjectReference) override;

private:
    void markHasContent();

    PDFDocumentTextFlow::Items* m_items;
    const PDFStructureTreeTextExtractor* m_extractor;
    std::vector<bool> m_hasContentStack;
};

void PDFStructureTreeTextFlowCollector::visitStructureTree(const PDFStructureTree* structureTree)
{
    m_items->push_back(PDFDocumentTextFlow::Item{PDFDocumentTextFlow::StructureItemStart, -1, QString()});
    acceptChildren(structureTree);
    m_items->push_back(PDFDocumentTextFlow::Item{PDFDocumentTextFlow::StructureItemEnd, -1, QString()});
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
    m_items->push_back(PDFDocumentTextFlow::Item{PDFDocumentTextFlow::StructureItemStart, -1, QString()});

    // Mark stack so we can delete unused items
    m_hasContentStack.push_back(false);

    QString title = structureElement->getText(PDFStructureElement::Title);
    QString language = structureElement->getText(PDFStructureElement::Language);
    QString alternativeDescription = structureElement->getText(PDFStructureElement::AlternativeDescription);
    QString expandedForm = structureElement->getText(PDFStructureElement::ExpandedForm);
    QString actualText = structureElement->getText(PDFStructureElement::ActualText);
    QString phoneme = structureElement->getText(PDFStructureElement::Phoneme);

    if (!title.isEmpty())
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{PDFDocumentTextFlow::StructureTitle, -1, });
    }

    if (!language.isEmpty())
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{PDFDocumentTextFlow::StructureLanguage, -1, language });
    }

    if (!alternativeDescription.isEmpty())
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{PDFDocumentTextFlow::StructureAlternativeDescription, -1, alternativeDescription });
    }

    if (!expandedForm.isEmpty())
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{PDFDocumentTextFlow::StructureExpandedForm, -1, expandedForm });
    }

    if (!actualText.isEmpty())
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{PDFDocumentTextFlow::StructureActualText, -1, actualText });
    }

    if (!phoneme.isEmpty())
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{PDFDocumentTextFlow::StructurePhoneme, -1, phoneme });
    }

    for (const QString& string : m_extractor->getText(structureElement))
    {
        markHasContent();
        m_items->push_back(PDFDocumentTextFlow::Item{PDFDocumentTextFlow::Text, -1, string});
    }

    acceptChildren(structureElement);

    const bool hasContent = m_hasContentStack.back();
    m_hasContentStack.pop_back();

    m_items->push_back(PDFDocumentTextFlow::Item{PDFDocumentTextFlow::StructureItemEnd, -1, QString()});

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

    // Perform algorithm to retrieve document text
    switch (algorithm)
    {
        case Algorithm::Layout:
        {
            PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);

            std::map<PDFInteger, PDFDocumentTextFlow::Items> items;

            QMutex mutex;
            PDFCMSGeneric cms;
            PDFMeshQualitySettings mqs;
            PDFOptionalContentActivity oca(document, OCUsage::Export, nullptr);
            pdf::PDFModifiedDocument md(const_cast<PDFDocument*>(document), &oca);
            fontCache.setDocument(md);
            fontCache.setCacheShrinkEnabled(nullptr, false);

            auto generateTextLayout = [this, &items, &mutex, &fontCache, &cms, &mqs, &oca, document, catalog](PDFInteger pageIndex)
            {
                if (!catalog->getPage(pageIndex))
                {
                    // Invalid page index
                    return;
                }

                const PDFPage* page = catalog->getPage(pageIndex);
                Q_ASSERT(page);

                PDFTextLayoutGenerator generator(PDFRenderer::IgnoreOptionalContent, page, document, &fontCache, &cms, &oca, QMatrix(), mqs);
                QList<PDFRenderError> errors = generator.processContents();
                PDFTextLayout textLayout = generator.createTextLayout();
                PDFTextFlows textFlows = PDFTextFlow::createTextFlows(textLayout, PDFTextFlow::FlowFlags(PDFTextFlow::SeparateBlocks) | PDFTextFlow::RemoveSoftHyphen, pageIndex);

                PDFDocumentTextFlow::Items flowItems;
                flowItems.emplace_back(PDFDocumentTextFlow::Item{ PDFDocumentTextFlow::PageStart, pageIndex, PDFTranslationContext::tr("Page %1").arg(pageIndex + 1) });
                for (const PDFTextFlow& textFlow : textFlows)
                {
                    flowItems.emplace_back(PDFDocumentTextFlow::Item{ PDFDocumentTextFlow::Text, pageIndex, textFlow.getText() });
                }
                flowItems.emplace_back(PDFDocumentTextFlow::Item{ PDFDocumentTextFlow::PageEnd, pageIndex, QString() });

                QMutexLocker lock(&mutex);
                items[pageIndex] = qMove(flowItems);
                m_errors.append(qMove(errors));
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

            PDFStructureTreeTextExtractor extractor(document, &structureTree, PDFStructureTreeTextExtractor::SkipArtifact | PDFStructureTreeTextExtractor::AdjustReversedText | PDFStructureTreeTextExtractor::CreateTreeMapping);
            extractor.perform(pageIndices);

            PDFDocumentTextFlow::Items flowItems;
            PDFStructureTreeTextFlowCollector collector(&flowItems, &extractor);
            structureTree.accept(&collector);

            result = PDFDocumentTextFlow(qMove(flowItems));
            m_errors.append(extractor.getErrors());
            break;
        }

        case Algorithm::Content:
        {
            PDFStructureTreeTextExtractor extractor(document, &structureTree, PDFStructureTreeTextExtractor::None);
            extractor.perform(pageIndices);

            PDFDocumentTextFlow::Items flowItems;
            for (PDFInteger pageIndex : pageIndices)
            {
                flowItems.emplace_back(PDFDocumentTextFlow::Item{ PDFDocumentTextFlow::PageStart, pageIndex, PDFTranslationContext::tr("Page %1").arg(pageIndex + 1) });
                for (const PDFStructureTreeTextItem& sequenceItem : extractor.getTextSequence(pageIndex))
                {
                    if (sequenceItem.type == PDFStructureTreeTextItem::Type::Text)
                    {
                        flowItems.emplace_back(PDFDocumentTextFlow::Item{ PDFDocumentTextFlow::Text, pageIndex, sequenceItem.text });
                    }
                }
                flowItems.emplace_back(PDFDocumentTextFlow::Item{ PDFDocumentTextFlow::PageEnd, pageIndex, QString() });
            }

            result = PDFDocumentTextFlow(qMove(flowItems));
            m_errors.append(extractor.getErrors());
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    return result;
}

}   // namespace pdf
