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

#include "pdfocrsession.h"
#include "pdfocrpagepreparer.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfdocument.h"
#include "pdfcatalog.h"
#include "pdfconstants.h"

#include <QRegularExpression>

#include <algorithm>

namespace pdf
{

PDFOCRSession::PDFOCRSession(QObject* parent) :
    QObject(parent)
{

}

PDFOCRSession::~PDFOCRSession()
{

}

void PDFOCRSession::setDocument(const PDFDocument* document, PDFOCRDocumentIdentity identity)
{
    m_document = document;
    m_identity = std::move(identity);
}

void PDFOCRSession::setConfiguration(PDFOCRConfiguration configuration)
{
    if (m_configuration == configuration)
    {
        return;
    }

    m_configuration = std::move(configuration);
    setDirty(true);
    Q_EMIT configurationChanged();
}

std::optional<PDFOCRPageOverride> PDFOCRSession::getPageOverride(PDFInteger pageIndex) const
{
    auto it = m_pageOverrides.find(pageIndex);
    if (it != m_pageOverrides.end())
    {
        return it->second;
    }
    return std::nullopt;
}

void PDFOCRSession::setPageOverride(PDFInteger pageIndex, PDFOCRPageOverride pageOverride)
{
    if (pageOverride.isEmpty())
    {
        clearPageOverride(pageIndex);
        return;
    }

    m_pageOverrides[pageIndex] = std::move(pageOverride);
    setDirty(true);
    Q_EMIT pageChanged(pageIndex);
}

void PDFOCRSession::clearPageOverride(PDFInteger pageIndex)
{
    if (m_pageOverrides.erase(pageIndex) > 0)
    {
        setDirty(true);
        Q_EMIT pageChanged(pageIndex);
    }
}

PDFOCRConfiguration PDFOCRSession::getEffectiveConfiguration(PDFInteger pageIndex) const
{
    std::optional<PDFOCRPageOverride> pageOverride = getPageOverride(pageIndex);
    return PDFOCRConfigurationResolver::resolve(m_configuration, pageOverride ? &*pageOverride : nullptr, nullptr);
}

// -------------------------------------------------------------------------
// Results
// -------------------------------------------------------------------------

const PDFOCRPageResult* PDFOCRSession::getPage(PDFInteger pageIndex) const
{
    auto it = m_pages.find(pageIndex);
    return it != m_pages.end() ? &it->second : nullptr;
}

PDFOCRPageResult& PDFOCRSession::getOrCreatePage(PDFInteger pageIndex)
{
    auto it = m_pages.find(pageIndex);
    if (it == m_pages.end())
    {
        PDFOCRPageResult result;
        result.pageIndex = pageIndex;
        result.state = PDFOCRPageState::Pending;
        if (m_document)
        {
            result.pageLabel = PDFOCRPagePreparer::getPageLabel(m_document, pageIndex);
        }
        it = m_pages.emplace(pageIndex, std::move(result)).first;
    }
    return it->second;
}

std::vector<PDFInteger> PDFOCRSession::getPages() const
{
    std::vector<PDFInteger> pages;
    for (const auto& item : m_pages)
    {
        pages.push_back(item.first);
    }
    return pages;
}

std::vector<PDFInteger> PDFOCRSession::getPagesWithResults() const
{
    std::vector<PDFInteger> pages;
    for (const auto& item : m_pages)
    {
        if (item.second.hasResult())
        {
            pages.push_back(item.first);
        }
    }
    return pages;
}

void PDFOCRSession::setPageResult(PDFOCRPageResult result)
{
    const PDFInteger pageIndex = result.pageIndex;
    PDFOCRPageResult& page = getOrCreatePage(pageIndex);

    if (result.regions.empty())
    {
        result.regions = page.regions;
    }
    if (result.pageLabel.isEmpty())
    {
        result.pageLabel = page.pageLabel;
    }
    result.blankDetectionOverridden = page.blankDetectionOverridden;
    result.assignIdentifiers();

    // Masks are checked against the current regions (REGION-05)
    PDFOCRPagePreparer::updateExcludedRegionFlags(result);
    for (PDFOCRWord* word : result.getWords())
    {
        updateWordFlags(*word);
    }

    // Raw recognition is kept next to the editable blocks (DATA-02). Identifiers
    // of the original words are the identifiers at the time of the recognition.
    if (result.originalBlocks.empty() && !result.blocks.empty())
    {
        result.originalBlocks = result.blocks;
    }

    page = std::move(result);
    clearHistoryOfPage(pageIndex);
    setDirty(true);
    Q_EMIT pageChanged(pageIndex);
}

void PDFOCRSession::setPageReviewOnly(PDFInteger pageIndex, bool reviewOnly)
{
    PDFOCRPageResult& page = getOrCreatePage(pageIndex);
    if (page.reviewOnly != reviewOnly)
    {
        page.reviewOnly = reviewOnly;
        setDirty(true);
        Q_EMIT pageChanged(pageIndex);
    }
}

void PDFOCRSession::rebindPageFingerprints(const std::map<PDFInteger, QByteArray>& fingerprints)
{
    for (const auto& [pageIndex, fingerprint] : fingerprints)
    {
        auto it = m_pages.find(pageIndex);
        if (it == m_pages.end() || !it->second.hasResult() || it->second.pageFingerprint == fingerprint)
        {
            continue;
        }

        it->second.pageFingerprint = fingerprint;
        setDirty(true);
        Q_EMIT pageChanged(pageIndex);
    }
}

const PDFOCRWord* PDFOCRSession::findOriginalWord(PDFInteger pageIndex, int wordId) const
{
    const PDFOCRPageResult* page = getPage(pageIndex);
    return page ? page->findOriginalWord(wordId) : nullptr;
}

std::vector<const PDFOCRWord*> PDFOCRSession::getOriginalWords(PDFInteger pageIndex) const
{
    const PDFOCRPageResult* page = getPage(pageIndex);
    return page ? page->getOriginalWords() : std::vector<const PDFOCRWord*>();
}

void PDFOCRSession::setPageState(PDFInteger pageIndex, PDFOCRPageState state)
{
    PDFOCRPageResult& page = getOrCreatePage(pageIndex);
    if (page.state != state)
    {
        page.state = state;
        Q_EMIT pageChanged(pageIndex);
    }
}

void PDFOCRSession::markPagesStale(const std::vector<PDFInteger>& pages)
{
    for (PDFInteger pageIndex : pages)
    {
        auto it = m_pages.find(pageIndex);
        if (it != m_pages.end() && it->second.hasResult())
        {
            it->second.state = PDFOCRPageState::Stale;
            Q_EMIT pageChanged(pageIndex);
        }
    }
}

void PDFOCRSession::clearPageResult(PDFInteger pageIndex)
{
    auto it = m_pages.find(pageIndex);
    if (it == m_pages.end())
    {
        return;
    }

    PDFOCRPageResult fresh;
    fresh.pageIndex = pageIndex;
    fresh.pageLabel = it->second.pageLabel;
    fresh.regions = it->second.regions;
    fresh.blankDetectionOverridden = it->second.blankDetectionOverridden;
    fresh.nextId = it->second.nextId;
    fresh.state = PDFOCRPageState::Pending;
    it->second = std::move(fresh);
    clearHistoryOfPage(pageIndex);
    setDirty(true);
    Q_EMIT pageChanged(pageIndex);
}

void PDFOCRSession::setBlankDetectionOverridden(PDFInteger pageIndex, bool overridden)
{
    PDFOCRPageResult& page = getOrCreatePage(pageIndex);
    page.blankDetectionOverridden = overridden;
    Q_EMIT pageChanged(pageIndex);
}

// -------------------------------------------------------------------------
// Undo helpers
// -------------------------------------------------------------------------

PDFOCRPageResult* PDFOCRSession::getEditablePage(PDFInteger pageIndex)
{
    auto it = m_pages.find(pageIndex);
    if (it == m_pages.end())
    {
        return nullptr;
    }

    if (it->second.state == PDFOCRPageState::Preparing || it->second.state == PDFOCRPageState::Recognizing)
    {
        // Page being processed cannot be edited (UI-06)
        return nullptr;
    }

    return &it->second;
}

bool PDFOCRSession::edit(const std::vector<PDFInteger>& pages, const QString& text, const std::function<bool()>& operation)
{
    UndoStep step;
    step.text = text;
    std::vector<PDFInteger> createdPages;
    for (PDFInteger pageIndex : pages)
    {
        if (!m_pages.count(pageIndex))
        {
            createdPages.push_back(pageIndex);
        }
        step.before.emplace_back(pageIndex, getOrCreatePage(pageIndex));
    }

    if (!operation())
    {
        // Failed operation leaves no trace
        for (PDFInteger pageIndex : createdPages)
        {
            m_pages.erase(pageIndex);
        }
        return false;
    }

    for (PDFInteger pageIndex : pages)
    {
        PDFOCRPageResult& page = getOrCreatePage(pageIndex);
        page.isModified = true;

        // Any change of the regions or of the geometry of the words can change the
        // conflicts with the masks, so the flags are recomputed after every step (REGION-05)
        PDFOCRPagePreparer::updateExcludedRegionFlags(page);
        step.after.emplace_back(pageIndex, page);
    }

    m_undoSteps.push_back(std::move(step));
    if (m_undoSteps.size() > MaximumUndoSteps)
    {
        m_undoSteps.erase(m_undoSteps.begin());
    }
    m_redoSteps.clear();

    setDirty(true);
    for (PDFInteger pageIndex : pages)
    {
        Q_EMIT pageChanged(pageIndex);
    }
    Q_EMIT undoRedoChanged();
    return true;
}

void PDFOCRSession::applyStep(const std::vector<std::pair<PDFInteger, PDFOCRPageResult>>& snapshot)
{
    for (const auto& item : snapshot)
    {
        // Properties, which are not part of the editing history, are kept
        std::optional<bool> blankDetectionOverridden;
        std::optional<bool> reviewOnly;
        bool isStale = false;
        auto it = m_pages.find(item.first);
        if (it != m_pages.end())
        {
            blankDetectionOverridden = it->second.blankDetectionOverridden;
            reviewOnly = it->second.reviewOnly;
            isStale = it->second.state == PDFOCRPageState::Stale;
        }

        PDFOCRPageResult& page = m_pages[item.first];
        page = item.second;

        if (blankDetectionOverridden)
        {
            page.blankDetectionOverridden = *blankDetectionOverridden;
        }
        if (reviewOnly)
        {
            page.reviewOnly = *reviewOnly;
        }
        if (isStale && page.hasResult())
        {
            page.state = PDFOCRPageState::Stale;
        }
    }

    setDirty(true);
    for (const auto& item : snapshot)
    {
        Q_EMIT pageChanged(item.first);
    }
    Q_EMIT undoRedoChanged();
}

QString PDFOCRSession::getUndoText() const
{
    return canUndo() ? m_undoSteps.back().text : QString();
}

QString PDFOCRSession::getRedoText() const
{
    return canRedo() ? m_redoSteps.back().text : QString();
}

bool PDFOCRSession::isStepApplicable(const UndoStep& step) const
{
    // Pages being processed cannot be changed (UI-06)
    for (const auto& item : step.before)
    {
        auto it = m_pages.find(item.first);
        if (it != m_pages.end() && (it->second.state == PDFOCRPageState::Preparing || it->second.state == PDFOCRPageState::Recognizing))
        {
            return false;
        }
    }
    return true;
}

void PDFOCRSession::clearHistoryOfPage(PDFInteger pageIndex)
{
    auto containsPage = [pageIndex](const UndoStep& step)
    {
        auto isPage = [pageIndex](const auto& item) { return item.first == pageIndex; };
        return std::any_of(step.before.begin(), step.before.end(), isPage) || std::any_of(step.after.begin(), step.after.end(), isPage);
    };

    const size_t oldCount = m_undoSteps.size() + m_redoSteps.size();
    std::erase_if(m_undoSteps, containsPage);
    std::erase_if(m_redoSteps, containsPage);

    if (oldCount != m_undoSteps.size() + m_redoSteps.size())
    {
        Q_EMIT undoRedoChanged();
    }
}

void PDFOCRSession::undo()
{
    if (!canUndo() || !isStepApplicable(m_undoSteps.back()))
    {
        return;
    }

    // The step is moved before the signals are emitted, so the listeners see the final state
    m_redoSteps.push_back(std::move(m_undoSteps.back()));
    m_undoSteps.pop_back();
    const auto snapshot = m_redoSteps.back().before;
    applyStep(snapshot);
}

void PDFOCRSession::redo()
{
    if (!canRedo() || !isStepApplicable(m_redoSteps.back()))
    {
        return;
    }

    m_undoSteps.push_back(std::move(m_redoSteps.back()));
    m_redoSteps.pop_back();
    const auto snapshot = m_undoSteps.back().after;
    applyStep(snapshot);
}

void PDFOCRSession::clearHistory()
{
    m_undoSteps.clear();
    m_redoSteps.clear();
    Q_EMIT undoRedoChanged();
}

void PDFOCRSession::setDirty(bool dirty)
{
    if (m_dirty != dirty)
    {
        m_dirty = dirty;
        Q_EMIT dirtyChanged(dirty);
    }
}

void PDFOCRSession::updateWordFlags(PDFOCRWord& word) const
{
    word.hasExtremeScaling = word.isUsable() && PDFOCRTextLayerWriter::isExtremeScaling(PDFOCRTextLayerWriter::computeHorizontalScaling(word));
}

QString PDFOCRSession::getRestorableText(const PDFOCRPageResult& page, const PDFOCRWord& word)
{
    if (!word.originalText.trimmed().isEmpty())
    {
        return word.originalText;
    }

    // Word without a historical text of its own (result of a merge, of a line
    // edit, ...): the original texts of its predecessors in the raw recognition
    // are concatenated (DATA-02). All predecessors must be available.
    if (word.predecessorIds.empty())
    {
        return QString();
    }

    QStringList texts;
    for (const int predecessorId : word.predecessorIds)
    {
        const PDFOCRWord* original = page.findOriginalWord(predecessorId);
        if (!original || original->originalText.trimmed().isEmpty())
        {
            return QString();
        }
        texts << original->originalText;
    }
    return texts.join(QChar(' '));
}

// -------------------------------------------------------------------------
// Regions
// -------------------------------------------------------------------------

int PDFOCRSession::addRegion(PDFInteger pageIndex, PDFOCRRegion region)
{
    int id = 0;
    edit({ pageIndex }, PDFTranslationContext::tr("Add region"), [&]()
    {
        PDFOCRPageResult& page = getOrCreatePage(pageIndex);
        if (!PDFOCRValidator::isValidRect(region.rect, 1.0e6))
        {
            return false;
        }

        region.id = page.allocateId();
        if (region.order == 0)
        {
            int maximumOrder = 0;
            for (const PDFOCRRegion& existing : page.regions)
            {
                maximumOrder = qMax(maximumOrder, existing.order);
            }
            region.order = maximumOrder + 1;
        }
        id = region.id;
        page.regions.push_back(std::move(region));
        return true;
    });
    return id;
}

bool PDFOCRSession::updateRegion(PDFInteger pageIndex, const PDFOCRRegion& region)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Change region"), [&]()
    {
        PDFOCRPageResult& page = getOrCreatePage(pageIndex);
        PDFOCRRegion* existing = page.findRegion(region.id);
        if (!existing || !PDFOCRValidator::isValidRect(region.rect, 1.0e6))
        {
            return false;
        }
        *existing = region;
        return true;
    });
}

bool PDFOCRSession::removeRegion(PDFInteger pageIndex, int regionId)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Remove region"), [&]()
    {
        PDFOCRPageResult& page = getOrCreatePage(pageIndex);
        const size_t oldSize = page.regions.size();
        std::erase_if(page.regions, [regionId](const PDFOCRRegion& region) { return region.id == regionId; });
        return page.regions.size() != oldSize;
    });
}

int PDFOCRSession::createRegionsFromBlocks(PDFInteger pageIndex)
{
    int count = 0;
    edit({ pageIndex }, PDFTranslationContext::tr("Create regions from blocks"), [&]()
    {
        PDFOCRPageResult& page = getOrCreatePage(pageIndex);
        int order = 0;
        for (const PDFOCRRegion& region : page.regions)
        {
            order = qMax(order, region.order);
        }

        for (PDFOCRBlock& block : page.blocks)
        {
            if (block.regionId != -1 || block.lines.empty())
            {
                continue;
            }

            QRectF rect;
            for (const PDFOCRLine& line : block.lines)
            {
                for (const PDFOCRWord& word : line.words)
                {
                    rect = rect.united(word.quad.boundingRect());
                }
            }

            if (!PDFOCRValidator::isValidRect(rect, 1.0e6))
            {
                continue;
            }

            const qreal margin = 2.0;
            PDFOCRRegion region;
            region.id = page.allocateId();
            region.type = PDFOCRRegionType::Recognize;
            region.rect = rect.adjusted(-margin, -margin, margin, margin);
            region.order = ++order;
            region.proposedByAnalysis = true;
            block.regionId = region.id;
            page.regions.push_back(std::move(region));
            ++count;
        }
        return count > 0;
    });
    return count;
}

// -------------------------------------------------------------------------
// Corrections
// -------------------------------------------------------------------------

bool PDFOCRSession::setWordText(PDFInteger pageIndex, int wordId, const QString& text)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Change text"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRWord* word = page ? page->findWord(wordId) : nullptr;
        if (!word || !PDFOCRValidator::isValidText(text))
        {
            return false;
        }

        if (word->text == text)
        {
            return false;
        }

        word->text = text;
        word->reviewState = PDFOCRReviewState::Modified;
        word->reviewTime = QDateTime::currentDateTime();
        updateWordFlags(*word);
        return true;
    });
}

bool PDFOCRSession::setWordQuad(PDFInteger pageIndex, int wordId, const PDFOCRQuad& quad)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Change geometry"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRWord* word = page ? page->findWord(wordId) : nullptr;
        if (!word || !quad.isValid())
        {
            return false;
        }

        word->quad = quad;
        word->geometryOrigin = PDFOCRGeometryOrigin::Manual;
        word->reviewState = PDFOCRReviewState::Modified;
        word->reviewTime = QDateTime::currentDateTime();
        updateWordFlags(*word);

        if (PDFOCRLine* line = page->findLineOfWord(wordId))
        {
            line->updateGeometryFromWords();
        }
        return true;
    });
}

bool PDFOCRSession::setWordReviewState(PDFInteger pageIndex, int wordId, PDFOCRReviewState state)
{
    QString text;
    switch (state)
    {
        case PDFOCRReviewState::Confirmed:
            text = PDFTranslationContext::tr("Confirm word");
            break;
        case PDFOCRReviewState::Discarded:
            text = PDFTranslationContext::tr("Mark as not text");
            break;
        case PDFOCRReviewState::Unreviewed:
            text = PDFTranslationContext::tr("Mark as unreviewed");
            break;
        case PDFOCRReviewState::Modified:
            text = PDFTranslationContext::tr("Mark as modified");
            break;
    }

    return edit({ pageIndex }, text, [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRWord* word = page ? page->findWord(wordId) : nullptr;
        if (!word || word->reviewState == state)
        {
            return false;
        }

        word->reviewState = state;
        word->reviewTime = QDateTime::currentDateTime();
        return true;
    });
}

bool PDFOCRSession::restoreOriginalText(PDFInteger pageIndex, int wordId)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Restore original recognition"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRWord* word = page ? page->findWord(wordId) : nullptr;
        if (!word)
        {
            return false;
        }

        const QString originalText = getRestorableText(*page, *word);
        if (originalText.trimmed().isEmpty())
        {
            // Word has no original recognition (inserted word, result of a line edit
            // without a raw recognition of its predecessors)
            return false;
        }

        if (word->text == originalText && word->reviewState == PDFOCRReviewState::Unreviewed)
        {
            return false;
        }

        word->text = originalText;
        word->reviewState = PDFOCRReviewState::Unreviewed;
        word->reviewTime.reset();
        updateWordFlags(*word);
        return true;
    });
}

bool PDFOCRSession::confirmAllWords(PDFInteger pageIndex, int* count)
{
    int confirmed = 0;
    const bool result = edit({ pageIndex }, PDFTranslationContext::tr("Confirm all words"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        if (!page)
        {
            return false;
        }

        for (PDFOCRWord* word : page->getWords())
        {
            if (word->reviewState == PDFOCRReviewState::Unreviewed || word->reviewState == PDFOCRReviewState::Modified)
            {
                word->reviewState = PDFOCRReviewState::Confirmed;
                word->reviewTime = QDateTime::currentDateTime();
                ++confirmed;
            }
        }
        return confirmed > 0;
    });

    if (count)
    {
        *count = confirmed;
    }
    return result;
}

bool PDFOCRSession::mergeWords(PDFInteger pageIndex, int firstWordId, int secondWordId, int* newWordId)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Merge words"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        if (!page)
        {
            return false;
        }

        PDFOCRLine* line = page->findLineOfWord(firstWordId);
        if (!line || line != page->findLineOfWord(secondWordId))
        {
            return false;
        }

        auto firstIt = std::find_if(line->words.begin(), line->words.end(), [firstWordId](const PDFOCRWord& word) { return word.id == firstWordId; });
        auto secondIt = std::find_if(line->words.begin(), line->words.end(), [secondWordId](const PDFOCRWord& word) { return word.id == secondWordId; });
        if (firstIt == line->words.end() || secondIt == line->words.end())
        {
            return false;
        }

        if (secondIt < firstIt)
        {
            std::swap(firstIt, secondIt);
        }

        if (std::next(firstIt) != secondIt)
        {
            // Words must be adjacent
            return false;
        }

        PDFOCRWord merged;
        merged.id = page->allocateId();
        merged.originalText = firstIt->originalText + QChar(' ') + secondIt->originalText;
        merged.text = firstIt->text + secondIt->text;
        // Quads are oriented visually, so the union in the writing direction is valid
        // for the left-to-right and also for the right-to-left text (EDIT-08)
        merged.quad = PDFOCRQuad::fromOrientedBounds(firstIt->quad.direction(), { firstIt->quad, secondIt->quad });
        merged.geometryOrigin = PDFOCRGeometryOrigin::Estimated;
        merged.textOrigin = firstIt->textOrigin;
        merged.language = firstIt->language;
        merged.confidence = PDFOCRConfidence::unknown();
        merged.reviewState = PDFOCRReviewState::Modified;
        merged.reviewTime = QDateTime::currentDateTime();
        merged.predecessorIds = { firstIt->id, secondIt->id };
        merged.overlapsExcludedRegion = firstIt->overlapsExcludedRegion || secondIt->overlapsExcludedRegion;

        if (!merged.quad.isValid())
        {
            merged.quad = PDFOCRQuad::fromRect(firstIt->quad.boundingRect().united(secondIt->quad.boundingRect()));
        }

        updateWordFlags(merged);

        if (newWordId)
        {
            *newWordId = merged.id;
        }

        *firstIt = std::move(merged);
        line->words.erase(secondIt);
        line->updateGeometryFromWords();
        return true;
    });
}

bool PDFOCRSession::splitWord(PDFInteger pageIndex, int wordId, int characterPosition, int* newWordId)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Split word"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        if (!page)
        {
            return false;
        }

        PDFOCRLine* line = page->findLineOfWord(wordId);
        if (!line)
        {
            return false;
        }

        auto it = std::find_if(line->words.begin(), line->words.end(), [wordId](const PDFOCRWord& word) { return word.id == wordId; });
        if (it == line->words.end())
        {
            return false;
        }

        const QString text = it->text;
        if (characterPosition <= 0 || characterPosition >= text.size())
        {
            return false;
        }

        if (text.at(characterPosition).isLowSurrogate() && text.at(characterPosition - 1).isHighSurrogate())
        {
            // Surrogate pair cannot be split (EDIT-08)
            return false;
        }

        const QString leftText = text.left(characterPosition).trimmed();
        const QString rightText = text.mid(characterPosition).trimmed();
        if (leftText.isEmpty() || rightText.isEmpty())
        {
            return false;
        }

        // Geometry is split proportionally by the character count (EDIT-03)
        // Quads are oriented visually: the first characters of a right-to-left word
        // lie in the right part of the quad.
        const bool rightToLeft = line->direction == PDFOCRTextDirection::RightToLeft;
        const double logicalRatio = double(leftText.size()) / double(leftText.size() + rightText.size());
        const double ratio = rightToLeft ? 1.0 - logicalRatio : logicalRatio;
        const PDFOCRQuad& quad = it->quad;
        const QPointF bottomSplit = quad.points[0] + (quad.points[1] - quad.points[0]) * ratio;
        const QPointF topSplit = quad.points[3] + (quad.points[2] - quad.points[3]) * ratio;

        // Original recognition can be restored only if the word was not edited before
        // the split; otherwise the halves have no original text of their own.
        const bool hasOriginal = it->originalText == text;

        PDFOCRWord left = *it;
        left.text = leftText;
        left.originalText = hasOriginal ? leftText : QString();
        if (rightToLeft)
        {
            left.quad.points[0] = bottomSplit;
            left.quad.points[3] = topSplit;
        }
        else
        {
            left.quad.points[1] = bottomSplit;
            left.quad.points[2] = topSplit;
        }
        left.geometryOrigin = PDFOCRGeometryOrigin::Estimated;
        left.reviewState = PDFOCRReviewState::Modified;
        left.reviewTime = QDateTime::currentDateTime();
        left.predecessorIds = { it->id };
        left.id = page->allocateId();
        updateWordFlags(left);

        PDFOCRWord right = *it;
        right.text = rightText;
        right.originalText = hasOriginal ? rightText : QString();
        if (rightToLeft)
        {
            right.quad.points[1] = bottomSplit;
            right.quad.points[2] = topSplit;
        }
        else
        {
            right.quad.points[0] = bottomSplit;
            right.quad.points[3] = topSplit;
        }
        right.geometryOrigin = PDFOCRGeometryOrigin::Estimated;
        right.reviewState = PDFOCRReviewState::Modified;
        right.reviewTime = QDateTime::currentDateTime();
        right.predecessorIds = { it->id };
        right.id = page->allocateId();
        updateWordFlags(right);

        if (newWordId)
        {
            *newWordId = right.id;
        }

        *it = std::move(left);
        line->words.insert(std::next(it), std::move(right));
        return true;
    });
}

bool PDFOCRSession::insertWord(PDFInteger pageIndex, int lineId, int afterWordId, const QString& text, const PDFOCRQuad& quad, int* newWordId)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Insert word"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRLine* line = page ? page->findLine(lineId) : nullptr;
        if (!line || text.trimmed().isEmpty() || !PDFOCRValidator::isValidText(text))
        {
            return false;
        }

        PDFOCRWord word;
        word.id = page->allocateId();
        word.originalText = QString();
        word.text = text.trimmed();
        word.quad = quad;
        word.geometryOrigin = quad.isValid() ? PDFOCRGeometryOrigin::Manual : PDFOCRGeometryOrigin::Estimated;
        word.textOrigin = PDFOCRTextOrigin::Manual;
        word.confidence = PDFOCRConfidence::unknown();
        word.reviewState = PDFOCRReviewState::Modified;
        word.reviewTime = QDateTime::currentDateTime();

        auto position = line->words.begin();
        if (afterWordId > 0)
        {
            auto it = std::find_if(line->words.begin(), line->words.end(), [afterWordId](const PDFOCRWord& item) { return item.id == afterWordId; });
            if (it == line->words.end())
            {
                return false;
            }
            position = std::next(it);
        }

        if (!word.quad.isValid())
        {
            // Estimate the geometry from the neighbours (EDIT-04): the word gets its own area
            const PDFOCRWord* previous = position != line->words.begin() ? &*std::prev(position) : nullptr;
            const PDFOCRWord* next = position != line->words.end() ? &*position : nullptr;
            const PDFOCRWord* reference = previous ? previous : next;
            if (!reference)
            {
                return false;
            }

            const double height = reference->quad.height();
            const double characterWidth = reference->text.isEmpty() ? height * 0.5 : reference->quad.width() / reference->text.size();
            const double width = qMax(characterWidth * word.text.size(), height * 0.5);
            const QPointF direction = reference->quad.direction();
            const QPointF normal(-direction.y(), direction.x());
            QPointF origin = previous ? previous->quad.points[1] + direction * (height * 0.25) : next->quad.points[0] - direction * (width + height * 0.25);

            word.quad.points[0] = origin;
            word.quad.points[1] = origin + direction * width;
            word.quad.points[2] = origin + direction * width + normal * height;
            word.quad.points[3] = origin + normal * height;
        }

        updateWordFlags(word);

        if (newWordId)
        {
            *newWordId = word.id;
        }

        line->words.insert(position, std::move(word));
        line->updateGeometryFromWords();
        return true;
    });
}

bool PDFOCRSession::insertLine(PDFInteger pageIndex, int blockId, const QString& text, const PDFOCRQuad& quad, int* newLineId)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Insert line"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        if (!page || !quad.isValid() || text.trimmed().isEmpty() || !PDFOCRValidator::isValidText(text))
        {
            return false;
        }

        const QStringList tokens = text.simplified().split(QChar(' '), Qt::SkipEmptyParts);
        if (tokens.isEmpty())
        {
            return false;
        }

        PDFOCRLine line;
        line.id = page->allocateId();
        line.quad = quad;
        line.baseline = QLineF(quad.points[0], quad.points[1]);

        // Words are distributed proportionally by the character count, with single space gaps
        int totalCharacters = 0;
        for (const QString& token : tokens)
        {
            totalCharacters += token.size();
        }
        const int gaps = tokens.size() - 1;
        const double unit = quad.width() / double(totalCharacters + gaps);
        const QPointF direction = quad.direction();
        const QPointF normal(-direction.y(), direction.x());
        const double height = quad.height();

        double offset = 0.0;
        for (const QString& token : tokens)
        {
            PDFOCRWord word;
            word.id = page->allocateId();
            word.text = token;
            word.textOrigin = PDFOCRTextOrigin::Manual;
            word.geometryOrigin = PDFOCRGeometryOrigin::Estimated;
            word.confidence = PDFOCRConfidence::unknown();
            word.reviewState = PDFOCRReviewState::Modified;
            word.reviewTime = QDateTime::currentDateTime();

            const double width = unit * token.size();
            const QPointF origin = quad.points[0] + direction * offset;
            word.quad.points[0] = origin;
            word.quad.points[1] = origin + direction * width;
            word.quad.points[2] = origin + direction * width + normal * height;
            word.quad.points[3] = origin + normal * height;
            offset += width + unit;

            updateWordFlags(word);
            line.words.push_back(std::move(word));
        }

        if (newLineId)
        {
            *newLineId = line.id;
        }

        PDFOCRBlock* block = page->findBlock(blockId);
        if (block)
        {
            block->lines.push_back(std::move(line));
            block->updateGeometryFromLines();
        }
        else
        {
            PDFOCRBlock newBlock;
            newBlock.id = page->allocateId();
            newBlock.type = PDFOCRBlockType::Text;
            newBlock.lines.push_back(std::move(line));
            newBlock.updateGeometryFromLines();
            page->blocks.push_back(std::move(newBlock));
        }

        if (page->state == PDFOCRPageState::NoText)
        {
            page->state = PDFOCRPageState::Done;
        }
        return true;
    });
}

bool PDFOCRSession::removeWord(PDFInteger pageIndex, int wordId)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Remove word"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRLine* line = page ? page->findLineOfWord(wordId) : nullptr;
        if (!line)
        {
            return false;
        }

        std::erase_if(line->words, [wordId](const PDFOCRWord& word) { return word.id == wordId; });
        if (line->words.empty())
        {
            const int lineId = line->id;
            if (PDFOCRBlock* block = page->findBlockOfLine(lineId))
            {
                std::erase_if(block->lines, [lineId](const PDFOCRLine& item) { return item.id == lineId; });
                if (block->lines.empty())
                {
                    const int blockId = block->id;
                    std::erase_if(page->blocks, [blockId](const PDFOCRBlock& item) { return item.id == blockId; });
                }
            }
        }
        else
        {
            line->updateGeometryFromWords();
        }
        return true;
    });
}

bool PDFOCRSession::removeLine(PDFInteger pageIndex, int lineId)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Remove line"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRBlock* block = page ? page->findBlockOfLine(lineId) : nullptr;
        if (!block)
        {
            return false;
        }

        std::erase_if(block->lines, [lineId](const PDFOCRLine& item) { return item.id == lineId; });
        if (block->lines.empty())
        {
            const int blockId = block->id;
            std::erase_if(page->blocks, [blockId](const PDFOCRBlock& item) { return item.id == blockId; });
        }
        return true;
    });
}

bool PDFOCRSession::setLineText(PDFInteger pageIndex, int lineId, const QString& text)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Change line text"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRLine* line = page ? page->findLine(lineId) : nullptr;
        return applyLineText(page, line, text);
    });
}

bool PDFOCRSession::applyLineText(PDFOCRPageResult* page, PDFOCRLine* line, const QString& text)
{
    if (!page || !line || !PDFOCRValidator::isValidText(text))
    {
        return false;
    }

    const QStringList newTokens = text.simplified().split(QChar(' '), Qt::SkipEmptyParts);

    // Old tokens: non-discarded words in order
    std::vector<PDFOCRWord> oldWords;
    for (const PDFOCRWord& word : line->words)
    {
        if (word.reviewState != PDFOCRReviewState::Discarded)
        {
            oldWords.push_back(word);
        }
    }

    if (newTokens.isEmpty())
    {
        return false;
    }

    // Alignment of the tokens by the longest common subsequence (EDIT-03):
    // unchanged tokens keep their geometry and identity.
    const int n = int(oldWords.size());
    const int m = newTokens.size();
    std::vector<std::vector<int>> lcs(size_t(n + 1), std::vector<int>(size_t(m + 1), 0));
    for (int i = n - 1; i >= 0; --i)
    {
        for (int j = m - 1; j >= 0; --j)
        {
            if (oldWords[size_t(i)].text == newTokens[j])
            {
                lcs[size_t(i)][size_t(j)] = lcs[size_t(i + 1)][size_t(j + 1)] + 1;
            }
            else
            {
                lcs[size_t(i)][size_t(j)] = qMax(lcs[size_t(i + 1)][size_t(j)], lcs[size_t(i)][size_t(j + 1)]);
            }
        }
    }

    std::vector<PDFOCRWord> result;

    // Writing direction: line quad, or the first word, if the line quad is not usable
    QPointF direction = line->quad.direction();
    if (!line->quad.isValid() && !oldWords.empty())
    {
        direction = oldWords.front().quad.direction();
    }
    const QPointF normal(-direction.y(), direction.x());

    // Quads are oriented visually and the words are stored in the logical order,
    // so the words of a right-to-left line flow against the writing direction of
    // the quad (EDIT-08). Layout works with the flow direction.
    const bool rightToLeft = line->direction == PDFOCRTextDirection::RightToLeft;
    const QPointF flow = rightToLeft ? -direction : direction;
    auto flowStart = [rightToLeft](const PDFOCRQuad& quad) { return rightToLeft ? quad.points[1] : quad.points[0]; };
    auto flowEnd = [rightToLeft](const PDFOCRQuad& quad) { return rightToLeft ? quad.points[0] : quad.points[1]; };
    auto createFlowQuad = [&](const QPointF& start, double width, double quadHeight)
    {
        const QPointF end = start + flow * width;
        const QPointF left = rightToLeft ? end : start;
        const QPointF right = rightToLeft ? start : end;

        PDFOCRQuad quad;
        quad.points[0] = left;
        quad.points[1] = right;
        quad.points[2] = right + normal * quadHeight;
        quad.points[3] = left + normal * quadHeight;
        return quad;
    };

    auto createEstimated = [&](int oldBegin, int oldEnd, int newBegin, int newEnd)
    {
        // Span of the changed run in the writing direction
        QPointF spanStart;
        QPointF spanEnd;
        double height = line->quad.height();

        if (oldEnd > oldBegin)
        {
            spanStart = flowStart(oldWords[size_t(oldBegin)].quad);
            spanEnd = flowEnd(oldWords[size_t(oldEnd - 1)].quad);
            height = oldWords[size_t(oldBegin)].quad.height();
        }
        else
        {
            // Insertion: place between the neighbours
            const bool hasPrevious = oldBegin > 0;
            const bool hasNext = oldBegin < n;
            const PDFOCRWord* reference = hasPrevious ? &oldWords[size_t(oldBegin - 1)] : (hasNext ? &oldWords[size_t(oldBegin)] : nullptr);
            if (reference)
            {
                height = reference->quad.height();
            }

            int characters = 0;
            for (int j = newBegin; j < newEnd; ++j)
            {
                characters += newTokens[j].size();
            }
            const double characterWidth = (reference && !reference->text.isEmpty()) ? reference->quad.width() / reference->text.size() : height * 0.5;
            const double width = characterWidth * (characters + (newEnd - newBegin - 1));

            if (hasPrevious && hasNext)
            {
                spanStart = flowEnd(oldWords[size_t(oldBegin - 1)].quad) + flow * (height * 0.2);
                spanEnd = flowStart(oldWords[size_t(oldBegin)].quad) - flow * (height * 0.2);
                if (QPointF::dotProduct(spanEnd - spanStart, flow) < width * 0.5 && !result.empty() && result.back().id == oldWords[size_t(oldBegin - 1)].id)
                {
                    // No room between the neighbours: the previous word is laid out again
                    // together with the inserted tokens over the available span (EDIT-03)
                    PDFOCRWord& previous = result.back();
                    spanStart = flowStart(previous.quad);
                    const double availableLength = QPointF::dotProduct(spanEnd - spanStart, flow);
                    const int totalCharacters = previous.text.size() + characters + (newEnd - newBegin);
                    const double unitLength = totalCharacters > 0 ? availableLength / double(totalCharacters) : 0.0;
                    const double previousWidth = unitLength * previous.text.size();
                    previous.quad = createFlowQuad(spanStart, previousWidth, height);
                    previous.geometryOrigin = PDFOCRGeometryOrigin::Estimated;
                    previous.reviewState = PDFOCRReviewState::Modified;
                    previous.reviewTime = QDateTime::currentDateTime();
                    updateWordFlags(previous);
                    spanStart = spanStart + flow * (previousWidth + unitLength);
                }
            }
            else if (hasPrevious)
            {
                spanStart = flowEnd(oldWords[size_t(oldBegin - 1)].quad) + flow * (height * 0.25);
                spanEnd = spanStart + flow * width;
            }
            else if (hasNext)
            {
                spanEnd = flowStart(oldWords[size_t(oldBegin)].quad) - flow * (height * 0.25);
                spanStart = spanEnd - flow * width;
            }
            else
            {
                spanStart = flowStart(line->quad);
                spanEnd = flowEnd(line->quad);
            }
        }

        const double spanLength = QPointF::dotProduct(spanEnd - spanStart, flow);
        int characters = 0;
        for (int j = newBegin; j < newEnd; ++j)
        {
            characters += newTokens[j].size();
        }
        const int gaps = newEnd - newBegin - 1;
        const double unit = characters + gaps > 0 ? spanLength / double(characters + gaps) : 0.0;

        std::vector<int> predecessors;
        for (int i = oldBegin; i < oldEnd; ++i)
        {
            predecessors.push_back(oldWords[size_t(i)].id);
        }

        double offset = 0.0;
        for (int j = newBegin; j < newEnd; ++j)
        {
            PDFOCRWord word;
            word.id = page->allocateId();
            word.text = newTokens[j];
            word.originalText = (oldEnd - oldBegin == 1 && newEnd - newBegin == 1) ? oldWords[size_t(oldBegin)].originalText : QString();
            word.textOrigin = (oldEnd > oldBegin) ? oldWords[size_t(oldBegin)].textOrigin : PDFOCRTextOrigin::Manual;
            word.language = (oldEnd > oldBegin) ? oldWords[size_t(oldBegin)].language : QString();
            word.confidence = (oldEnd - oldBegin == 1 && newEnd - newBegin == 1) ? oldWords[size_t(oldBegin)].confidence : PDFOCRConfidence::unknown();
            word.inDictionary = (oldEnd - oldBegin == 1 && newEnd - newBegin == 1) ? oldWords[size_t(oldBegin)].inDictionary : std::nullopt;
            word.geometryOrigin = (oldEnd - oldBegin == 1 && newEnd - newBegin == 1) ? oldWords[size_t(oldBegin)].geometryOrigin : PDFOCRGeometryOrigin::Estimated;
            word.reviewState = PDFOCRReviewState::Modified;
            word.reviewTime = QDateTime::currentDateTime();
            word.predecessorIds = predecessors;

            if (oldEnd - oldBegin == 1 && newEnd - newBegin == 1)
            {
                // Single word replaced by a single word: keep the geometry and identity
                word.id = oldWords[size_t(oldBegin)].id;
                word.predecessorIds = oldWords[size_t(oldBegin)].predecessorIds;
                word.quad = oldWords[size_t(oldBegin)].quad;
            }
            else
            {
                const double width = unit * newTokens[j].size();
                word.quad = createFlowQuad(spanStart + flow * offset, width, height);
                offset += width + unit;
            }

            updateWordFlags(word);
            result.push_back(std::move(word));
        }
    };

    int i = 0;
    int j = 0;
    int changedOldBegin = 0;
    int changedNewBegin = 0;
    bool changed = false;

    while (i < n || j < m)
    {
        const bool match = i < n && j < m && oldWords[size_t(i)].text == newTokens[j] && lcs[size_t(i)][size_t(j)] == lcs[size_t(i + 1)][size_t(j + 1)] + 1;
        if (match)
        {
            if (i > changedOldBegin || j > changedNewBegin)
            {
                createEstimated(changedOldBegin, i, changedNewBegin, j);
                changed = true;
            }
            result.push_back(oldWords[size_t(i)]);
            ++i;
            ++j;
            changedOldBegin = i;
            changedNewBegin = j;
        }
        else if (i < n && (j >= m || lcs[size_t(i + 1)][size_t(j)] >= lcs[size_t(i)][size_t(j + 1)]))
        {
            ++i;
        }
        else
        {
            ++j;
        }
    }

    if (i > changedOldBegin || j > changedNewBegin)
    {
        createEstimated(changedOldBegin, i, changedNewBegin, j);
        changed = true;
    }

    if (!changed)
    {
        return false;
    }

    // Discarded words are kept at the end (they are not written anyway)
    for (const PDFOCRWord& word : line->words)
    {
        if (word.reviewState == PDFOCRReviewState::Discarded)
        {
            result.push_back(word);
        }
    }

    line->words = std::move(result);
    line->updateGeometryFromWords();
    return true;
}

bool PDFOCRSession::moveBlock(PDFInteger pageIndex, int blockId, int newIndex)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Change block order"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        if (!page)
        {
            return false;
        }

        auto it = std::find_if(page->blocks.begin(), page->blocks.end(), [blockId](const PDFOCRBlock& block) { return block.id == blockId; });
        if (it == page->blocks.end())
        {
            return false;
        }

        const int oldIndex = int(std::distance(page->blocks.begin(), it));
        const int target = qBound(0, newIndex, int(page->blocks.size()) - 1);
        if (oldIndex == target)
        {
            return false;
        }

        PDFOCRBlock block = std::move(*it);
        page->blocks.erase(it);
        page->blocks.insert(page->blocks.begin() + target, std::move(block));
        return true;
    });
}

bool PDFOCRSession::moveLine(PDFInteger pageIndex, int lineId, int newIndex)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Change line order"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRBlock* block = page ? page->findBlockOfLine(lineId) : nullptr;
        if (!block)
        {
            return false;
        }

        auto it = std::find_if(block->lines.begin(), block->lines.end(), [lineId](const PDFOCRLine& line) { return line.id == lineId; });
        const int oldIndex = int(std::distance(block->lines.begin(), it));
        const int target = qBound(0, newIndex, int(block->lines.size()) - 1);
        if (oldIndex == target)
        {
            return false;
        }

        PDFOCRLine line = std::move(*it);
        block->lines.erase(it);
        block->lines.insert(block->lines.begin() + target, std::move(line));
        return true;
    });
}

bool PDFOCRSession::setLineBaseline(PDFInteger pageIndex, int lineId, const QLineF& baseline)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Change baseline"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRLine* line = page ? page->findLine(lineId) : nullptr;
        if (!line || baseline.isNull())
        {
            return false;
        }

        line->baseline = baseline;
        return true;
    });
}

bool PDFOCRSession::mergeLines(PDFInteger pageIndex, int firstLineId, int secondLineId, int* newLineId)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Merge lines"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRBlock* block = page ? page->findBlockOfLine(firstLineId) : nullptr;
        if (!block || firstLineId == secondLineId || block != page->findBlockOfLine(secondLineId))
        {
            // Lines must be different lines of the same block
            return false;
        }

        auto firstIt = std::find_if(block->lines.begin(), block->lines.end(), [firstLineId](const PDFOCRLine& line) { return line.id == firstLineId; });
        auto secondIt = std::find_if(block->lines.begin(), block->lines.end(), [secondLineId](const PDFOCRLine& line) { return line.id == secondLineId; });
        if (firstIt == block->lines.end() || secondIt == block->lines.end() || firstIt->direction != secondIt->direction)
        {
            return false;
        }

        // The words of the second line are appended after the words of the first line
        // in the logical order and keep their identifiers. Quads are oriented visually,
        // so the oriented union in the writing direction is valid for the left-to-right
        // and also for the right-to-left text (EDIT-08).
        PDFOCRLine merged;
        merged.id = page->allocateId();
        merged.direction = firstIt->direction;
        merged.confidence = firstIt->confidence;
        merged.words = firstIt->words;
        merged.words.insert(merged.words.end(), secondIt->words.begin(), secondIt->words.end());

        const QPointF direction = firstIt->quad.isValid() ? firstIt->quad.direction() : secondIt->quad.direction();
        merged.quad = PDFOCRQuad::fromOrientedBounds(direction, { firstIt->quad, secondIt->quad });
        merged.baseline = QLineF(firstIt->baseline.p1(), secondIt->baseline.p2());

        if (!merged.quad.isValid() || merged.baseline.isNull())
        {
            merged.updateGeometryFromWords();
        }

        if (newLineId)
        {
            *newLineId = merged.id;
        }

        // The merged line takes the place of the first line
        const size_t firstIndex = size_t(std::distance(block->lines.begin(), firstIt));
        block->lines.erase(secondIt);
        block->lines[firstIndex] = std::move(merged);
        block->updateGeometryFromLines();
        return true;
    });
}

bool PDFOCRSession::splitLine(PDFInteger pageIndex, int lineId, int afterWordId, int* newLineId)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Split line"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRBlock* block = page ? page->findBlockOfLine(lineId) : nullptr;
        if (!block)
        {
            return false;
        }

        auto lineIt = std::find_if(block->lines.begin(), block->lines.end(), [lineId](const PDFOCRLine& line) { return line.id == lineId; });
        if (lineIt == block->lines.end())
        {
            return false;
        }

        auto wordIt = std::find_if(lineIt->words.begin(), lineIt->words.end(), [afterWordId](const PDFOCRWord& word) { return word.id == afterWordId; });
        if (wordIt == lineIt->words.end() || std::next(wordIt) == lineIt->words.end())
        {
            // Word must be in the line and must not be the last word (both lines need words)
            return false;
        }

        // The new line inherits the orientation, the direction and the line level
        // score; its geometry is recomputed from its words (in the logical order,
        // the visual layout is decided by the direction, EDIT-08)
        PDFOCRLine newLine;
        newLine.id = page->allocateId();
        newLine.direction = lineIt->direction;
        newLine.confidence = lineIt->confidence;
        newLine.quad = lineIt->quad;
        newLine.words.assign(std::next(wordIt), lineIt->words.end());
        lineIt->words.erase(std::next(wordIt), lineIt->words.end());

        newLine.updateGeometryFromWords();
        lineIt->updateGeometryFromWords();

        if (newLineId)
        {
            *newLineId = newLine.id;
        }

        block->lines.insert(std::next(lineIt), std::move(newLine));
        block->updateGeometryFromLines();
        return true;
    });
}

bool PDFOCRSession::moveLineToBlock(PDFInteger pageIndex, int lineId, int targetBlockId, int newIndex)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Move line to block"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRBlock* sourceBlock = page ? page->findBlockOfLine(lineId) : nullptr;
        PDFOCRBlock* targetBlock = page ? page->findBlock(targetBlockId) : nullptr;
        if (!sourceBlock || !targetBlock)
        {
            return false;
        }

        auto lineIt = std::find_if(sourceBlock->lines.begin(), sourceBlock->lines.end(), [lineId](const PDFOCRLine& line) { return line.id == lineId; });
        if (lineIt == sourceBlock->lines.end())
        {
            return false;
        }

        if (sourceBlock == targetBlock)
        {
            // Reorder inside the block
            const int oldIndex = int(std::distance(sourceBlock->lines.begin(), lineIt));
            const int target = qBound(0, newIndex, int(sourceBlock->lines.size()) - 1);
            if (oldIndex == target)
            {
                return false;
            }

            PDFOCRLine line = std::move(*lineIt);
            sourceBlock->lines.erase(lineIt);
            sourceBlock->lines.insert(sourceBlock->lines.begin() + target, std::move(line));
            return true;
        }

        PDFOCRLine line = std::move(*lineIt);
        sourceBlock->lines.erase(lineIt);

        const int target = qBound(0, newIndex, int(targetBlock->lines.size()));
        targetBlock->lines.insert(targetBlock->lines.begin() + target, std::move(line));
        targetBlock->updateGeometryFromLines();

        if (sourceBlock->lines.empty())
        {
            // Empty source block is removed
            const int sourceBlockId = sourceBlock->id;
            std::erase_if(page->blocks, [sourceBlockId](const PDFOCRBlock& block) { return block.id == sourceBlockId; });
        }
        else
        {
            sourceBlock->updateGeometryFromLines();
        }
        return true;
    });
}

// -------------------------------------------------------------------------
// Candidates
// -------------------------------------------------------------------------

bool PDFOCRSession::hasManualCorrections(PDFInteger pageIndex) const
{
    const PDFOCRPageResult* page = getPage(pageIndex);
    if (!page)
    {
        return false;
    }

    // Structural changes (removed words and lines, changed order, baseline) leave
    // no review state, they are tracked by the modification flag of the page (EDIT-07).
    if (page->isModified && page->hasResult())
    {
        return true;
    }

    for (const PDFOCRWord* word : page->getWords())
    {
        if (word->reviewState != PDFOCRReviewState::Unreviewed)
        {
            return true;
        }
    }
    return false;
}

bool PDFOCRSession::applyCandidate(PDFInteger pageIndex, const PDFOCRPageResult& candidate, CandidateMode mode, int regionId)
{
    if (mode == CandidateMode::Keep)
    {
        return false;
    }

    return edit({ pageIndex }, PDFTranslationContext::tr("Apply repeated recognition"), [&]()
    {
        PDFOCRPageResult* editablePage = getEditablePage(pageIndex);
        if (!editablePage)
        {
            return false;
        }

        PDFOCRPageResult& page = *editablePage;

        if (mode == CandidateMode::Replace)
        {
            PDFOCRPageResult result = candidate;
            result.regions = page.regions;
            result.blankDetectionOverridden = page.blankDetectionOverridden;
            result.reviewOnly = page.reviewOnly;
            result.nextId = qMax(result.nextId, page.nextId);
            result.assignIdentifiers();
            for (PDFOCRWord* word : result.getWords())
            {
                updateWordFlags(*word);
            }

            // Raw recognition of the candidate replaces the raw recognition of the page (DATA-02)
            if (result.originalBlocks.empty())
            {
                result.originalBlocks = result.blocks;
            }
            page = std::move(result);
            return true;
        }

        // ReplaceRegion: blocks of the region are replaced by the candidate blocks of
        // the region at the position of the first replaced block, so the reading
        // order of the page is kept (EDIT-09)
        auto isRegionBlock = [regionId](const PDFOCRBlock& block) { return block.regionId == regionId; };
        const auto firstIt = std::find_if(page.blocks.begin(), page.blocks.end(), isRegionBlock);
        size_t insertPosition = size_t(std::distance(page.blocks.begin(), firstIt));
        std::erase_if(page.blocks, isRegionBlock);
        insertPosition = qMin(insertPosition, page.blocks.size());

        std::vector<PDFOCRBlock> newBlocks;
        for (const PDFOCRBlock& block : candidate.blocks)
        {
            if (block.regionId == regionId)
            {
                // Identifiers of the candidate can collide with the identifiers of the
                // page, new blocks get new identifiers (existing units keep their identity)
                PDFOCRBlock newBlock = block;
                newBlock.id = 0;
                for (PDFOCRLine& line : newBlock.lines)
                {
                    line.id = 0;
                    for (PDFOCRWord& word : line.words)
                    {
                        word.id = 0;
                    }
                }
                newBlocks.push_back(std::move(newBlock));
            }
        }
        const auto insertIt = page.blocks.insert(std::next(page.blocks.begin(), std::ptrdiff_t(insertPosition)), newBlocks.begin(), newBlocks.end());
        page.assignIdentifiers();
        for (PDFOCRWord* word : page.getWords())
        {
            updateWordFlags(*word);
        }

        // Raw recognition of the region is replaced by the new blocks of the region
        // (with their final identifiers), raw recognition of the other regions is kept (DATA-02)
        const auto firstOriginalIt = std::find_if(page.originalBlocks.begin(), page.originalBlocks.end(), isRegionBlock);
        size_t originalInsertPosition = size_t(std::distance(page.originalBlocks.begin(), firstOriginalIt));
        std::erase_if(page.originalBlocks, isRegionBlock);
        originalInsertPosition = qMin(originalInsertPosition, page.originalBlocks.size());
        page.originalBlocks.insert(std::next(page.originalBlocks.begin(), std::ptrdiff_t(originalInsertPosition)), insertIt, std::next(insertIt, std::ptrdiff_t(newBlocks.size())));

        if (page.state == PDFOCRPageState::NoText && page.hasUsableText())
        {
            page.state = PDFOCRPageState::Done;
        }
        return true;
    });
}

bool PDFOCRSession::replaceLineWords(PDFInteger pageIndex, int lineId, const std::vector<PDFOCRWord>& words)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Recognize line again"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRLine* line = page ? page->findLine(lineId) : nullptr;
        if (!line || words.empty())
        {
            return false;
        }

        std::vector<int> predecessors;
        for (const PDFOCRWord& word : line->words)
        {
            predecessors.push_back(word.id);
        }

        line->words.clear();
        for (PDFOCRWord word : words)
        {
            word.id = page->allocateId();
            word.predecessorIds = predecessors;
            updateWordFlags(word);
            line->words.push_back(std::move(word));
        }
        line->updateGeometryFromWords();
        return true;
    });
}

bool PDFOCRSession::replaceWord(PDFInteger pageIndex, int wordId, const std::vector<PDFOCRWord>& words)
{
    return edit({ pageIndex }, PDFTranslationContext::tr("Recognize word again"), [&]()
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        PDFOCRLine* line = page ? page->findLineOfWord(wordId) : nullptr;
        if (!line || words.empty())
        {
            return false;
        }

        auto it = std::find_if(line->words.begin(), line->words.end(), [wordId](const PDFOCRWord& word) { return word.id == wordId; });
        if (it == line->words.end())
        {
            return false;
        }

        const size_t index = size_t(std::distance(line->words.begin(), it));
        line->words.erase(it);

        size_t offset = 0;
        for (PDFOCRWord word : words)
        {
            word.id = page->allocateId();
            word.predecessorIds = { wordId };
            updateWordFlags(word);
            line->words.insert(line->words.begin() + index + offset, std::move(word));
            ++offset;
        }
        line->updateGeometryFromWords();
        return true;
    });
}

// -------------------------------------------------------------------------
// Find and replace
// -------------------------------------------------------------------------

static QRegularExpression createFindExpression(const QString& text, const PDFOCRSession::FindOptions& options)
{
    QString pattern = QRegularExpression::escape(text);
    if (options.wholeWords)
    {
        pattern = QStringLiteral("(?<!\\w)") + pattern + QStringLiteral("(?!\\w)");
    }

    QRegularExpression::PatternOptions patternOptions = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.caseSensitive)
    {
        patternOptions |= QRegularExpression::CaseInsensitiveOption;
    }

    return QRegularExpression(pattern, patternOptions);
}

std::vector<PDFOCRSession::FindHit> PDFOCRSession::findInLine(const PDFOCRLine& line, const QRegularExpression& expression)
{
    // The search runs over the text of the line (the words joined by a single space,
    // discarded words skipped, exactly as PDFOCRLine::getText), so a phrase spanning
    // the word boundaries is found too (EDIT-05).
    struct Span
    {
        int wordId = 0;
        int start = 0;
        int end = 0;
    };

    std::vector<Span> spans;
    QString text;
    for (const PDFOCRWord& word : line.words)
    {
        if (word.reviewState == PDFOCRReviewState::Discarded)
        {
            continue;
        }

        if (!spans.empty())
        {
            text += QChar(' ');
        }

        Span span;
        span.wordId = word.id;
        span.start = int(text.size());
        span.end = span.start + int(word.text.size());
        spans.push_back(span);
        text += word.text;
    }

    std::vector<FindHit> hits;
    if (spans.empty())
    {
        return hits;
    }

    QRegularExpressionMatchIterator it = expression.globalMatch(text);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const int start = int(match.capturedStart());
        const int length = int(match.capturedLength());
        const int end = start + length;

        FindHit hit;
        hit.lineId = line.id;
        hit.length = length;
        for (const Span& span : spans)
        {
            if (span.end > start && span.start < end)
            {
                hit.wordIds.push_back(span.wordId);
            }
        }

        if (hit.wordIds.empty())
        {
            // Only the separating space was matched
            continue;
        }

        hit.wordId = hit.wordIds.front();
        const auto spanIt = std::find_if(spans.begin(), spans.end(), [&hit](const Span& span) { return span.wordId == hit.wordId; });
        hit.singleWord = hit.wordIds.size() == 1 && start >= spanIt->start && end <= spanIt->end;
        hit.position = hit.singleWord ? start - spanIt->start : start;
        hits.push_back(std::move(hit));
    }

    return hits;
}

std::vector<PDFOCRSession::FindHit> PDFOCRSession::find(const std::vector<PDFInteger>& pages, const QString& text, const FindOptions& options) const
{
    std::vector<FindHit> hits;
    if (text.isEmpty())
    {
        return hits;
    }

    const QRegularExpression expression = createFindExpression(text, options);

    for (PDFInteger pageIndex : pages)
    {
        const PDFOCRPageResult* page = getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        for (const PDFOCRBlock& block : page->blocks)
        {
            for (const PDFOCRLine& line : block.lines)
            {
                for (FindHit& hit : findInLine(line, expression))
                {
                    hit.pageIndex = pageIndex;
                    hits.push_back(std::move(hit));
                }
            }
        }
    }

    return hits;
}

int PDFOCRSession::replaceAll(const std::vector<PDFInteger>& pages, const QString& text, const QString& replacement, const FindOptions& options)
{
    if (text.isEmpty())
    {
        return 0;
    }

    const QRegularExpression expression = createFindExpression(text, options);
    int count = 0;

    // Only the pages with hits are part of the undo step and get the modification flag
    std::vector<PDFInteger> editablePages;
    for (PDFInteger pageIndex : pages)
    {
        PDFOCRPageResult* page = getEditablePage(pageIndex);
        if (!page)
        {
            continue;
        }

        bool hasHit = false;
        for (const PDFOCRBlock& block : page->blocks)
        {
            for (const PDFOCRLine& line : block.lines)
            {
                if (!findInLine(line, expression).empty())
                {
                    hasHit = true;
                    break;
                }
            }
            if (hasHit)
            {
                break;
            }
        }

        if (hasHit)
        {
            editablePages.push_back(pageIndex);
        }
    }

    if (editablePages.empty())
    {
        return 0;
    }

    edit(editablePages, PDFTranslationContext::tr("Replace all"), [&]()
    {
        for (PDFInteger pageIndex : editablePages)
        {
            PDFOCRPageResult* page = getEditablePage(pageIndex);
            for (PDFOCRBlock& block : page->blocks)
            {
                for (PDFOCRLine& line : block.lines)
                {
                    const std::vector<FindHit> hits = findInLine(line, expression);
                    if (hits.empty())
                    {
                        continue;
                    }

                    const bool spansWords = std::any_of(hits.begin(), hits.end(), [](const FindHit& hit) { return !hit.singleWord; });
                    if (!spansWords)
                    {
                        // All hits lie inside single words: the texts of the words are changed
                        for (PDFOCRWord& word : line.words)
                        {
                            if (word.reviewState == PDFOCRReviewState::Discarded)
                            {
                                continue;
                            }

                            QString newText = word.text;
                            const int replaced = int(newText.count(expression));
                            if (replaced == 0)
                            {
                                continue;
                            }

                            newText.replace(expression, replacement);
                            if (newText.trimmed().isEmpty())
                            {
                                // Replacing by an empty text discards the word
                                word.reviewState = PDFOCRReviewState::Discarded;
                            }
                            else
                            {
                                word.text = newText;
                                word.reviewState = PDFOCRReviewState::Modified;
                            }
                            word.reviewTime = QDateTime::currentDateTime();
                            updateWordFlags(word);
                            count += replaced;
                        }
                        continue;
                    }

                    // A hit spans the words: the substring of the line text is replaced and
                    // the line is tokenized again, unchanged words keep their identity (EDIT-05)
                    QString newText = line.getText();
                    newText.replace(expression, replacement);
                    count += int(hits.size());

                    if (newText.simplified().isEmpty())
                    {
                        // Replacing the whole line by an empty text discards its words
                        for (PDFOCRWord& word : line.words)
                        {
                            if (word.reviewState != PDFOCRReviewState::Discarded)
                            {
                                word.reviewState = PDFOCRReviewState::Discarded;
                                word.reviewTime = QDateTime::currentDateTime();
                            }
                        }
                        continue;
                    }

                    applyLineText(page, &line, newText);
                }
            }
        }
        return count > 0;
    });

    return count;
}

// -------------------------------------------------------------------------
// Review navigation
// -------------------------------------------------------------------------

std::optional<PDFOCRSession::WordReference> PDFOCRSession::findReviewItem(const std::vector<PDFInteger>& pages, const WordReference& start, bool forward) const
{
    // Flatten all words of the pages in reading order
    std::vector<WordReference> items;
    size_t startIndex = 0;
    bool startFound = false;

    for (PDFInteger pageIndex : pages)
    {
        const PDFOCRPageResult* page = getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        for (const PDFOCRWord* word : page->getWords())
        {
            if (start.isValid() && start.pageIndex == pageIndex && start.wordId == word->id)
            {
                startIndex = items.size();
                startFound = true;
            }
            items.push_back(WordReference{ pageIndex, word->id });
        }
    }

    if (items.empty())
    {
        return std::nullopt;
    }

    const PDFOCRReviewCriteria criteria = getReviewCriteria();
    auto requiresReview = [&](const WordReference& reference)
    {
        const PDFOCRPageResult* page = getPage(reference.pageIndex);
        const PDFOCRWord* word = page ? page->findWord(reference.wordId) : nullptr;
        return word && PDFOCRReview::requiresReview(*word, criteria);
    };

    const size_t count = items.size();
    size_t index = startFound ? startIndex : (forward ? count - 1 : 0);
    for (size_t step = 0; step < count; ++step)
    {
        index = forward ? (index + 1) % count : (index + count - 1) % count;
        if (requiresReview(items[index]))
        {
            return items[index];
        }
    }

    return std::nullopt;
}

std::vector<int> PDFOCRSession::getReviewWords(PDFInteger pageIndex) const
{
    std::vector<int> result;
    const PDFOCRPageResult* page = getPage(pageIndex);
    if (!page)
    {
        return result;
    }

    const PDFOCRReviewCriteria criteria = getReviewCriteria();
    for (const PDFOCRWord* word : page->getWords())
    {
        if (PDFOCRReview::requiresReview(*word, criteria))
        {
            result.push_back(word->id);
        }
    }
    return result;
}

// -------------------------------------------------------------------------
// Statistics
// -------------------------------------------------------------------------

PDFOCRReviewCriteria PDFOCRSession::getReviewCriteria() const
{
    return PDFOCRReviewCriteria::create(m_configuration.reviewThreshold, m_configuration.reviewOutsideDictionary, m_configuration.userWords);
}

PDFOCRConfidenceStatistics PDFOCRSession::getStatistics(PDFInteger pageIndex) const
{
    const PDFOCRPageResult* page = getPage(pageIndex);
    if (!page)
    {
        return PDFOCRConfidenceStatistics();
    }
    return PDFOCRConfidenceStatistics::compute(*page, getReviewCriteria());
}

PDFOCRConfidenceStatistics PDFOCRSession::getStatistics(const std::vector<PDFInteger>& pages) const
{
    PDFOCRConfidenceStatistics statistics;
    for (PDFInteger pageIndex : pages)
    {
        statistics.merge(getStatistics(pageIndex));
    }
    return statistics;
}

// -------------------------------------------------------------------------
// Project
// -------------------------------------------------------------------------

PDFOCRProject PDFOCRSession::createProject(const std::vector<PDFInteger>& selectedPages) const
{
    PDFOCRProject project;
    project.document = m_identity;
    project.configuration = m_configuration;
    project.pageOverrides = m_pageOverrides;
    project.selectedPages = selectedPages;
    project.created = QDateTime::currentDateTime();
    project.modified = project.created;
    project.application = QString::fromLatin1(PDF_LIBRARY_NAME);
    project.containsOriginalTexts = true;
    project.containsPreviews = false;

    for (const auto& item : m_pages)
    {
        // Pages with a result, and also the pages with a recorded outcome (skipped,
        // cancelled, failed) with their reasons are part of the project (EXPORT-02, EXPORT-03)
        const PDFOCRPageState state = item.second.state;
        const bool hasOutcome = state == PDFOCRPageState::Stale || state == PDFOCRPageState::Error ||
                                state == PDFOCRPageState::Skipped || state == PDFOCRPageState::Cancelled;
        if (item.second.hasResult() || hasOutcome || !item.second.regions.empty())
        {
            PDFOCRPageResult result = item.second;
            if (result.state == PDFOCRPageState::Preparing || result.state == PDFOCRPageState::Recognizing)
            {
                result.state = PDFOCRPageState::Pending;
            }
            project.pages[item.first] = std::move(result);
        }
    }

    return project;
}

void PDFOCRSession::loadProject(const PDFOCRProject& project, const std::vector<PDFInteger>& pages)
{
    m_configuration = project.configuration;
    m_pageOverrides = project.pageOverrides;

    // Results of the session are replaced by the project, no old page may survive
    m_pages.clear();

    for (PDFInteger pageIndex : pages)
    {
        auto it = project.pages.find(pageIndex);
        if (it == project.pages.end())
        {
            continue;
        }

        PDFOCRPageResult result = it->second;
        if (result.state == PDFOCRPageState::Preparing || result.state == PDFOCRPageState::Recognizing)
        {
            result.state = PDFOCRPageState::Pending;
        }
        if (m_document && result.pageLabel.isEmpty())
        {
            result.pageLabel = PDFOCRPagePreparer::getPageLabel(m_document, pageIndex);
        }
        result.assignIdentifiers();
        PDFOCRPagePreparer::updateExcludedRegionFlags(result);
        for (PDFOCRWord* word : result.getWords())
        {
            updateWordFlags(*word);
        }
        m_pages[pageIndex] = std::move(result);
    }

    clearHistory();
    setDirty(false);
    Q_EMIT configurationChanged();
    Q_EMIT pagesChanged();
}

std::vector<const PDFOCRPageResult*> PDFOCRSession::getResults(const std::vector<PDFInteger>& pages) const
{
    std::vector<const PDFOCRPageResult*> results;
    for (PDFInteger pageIndex : pages)
    {
        if (const PDFOCRPageResult* page = getPage(pageIndex))
        {
            results.push_back(page);
        }
    }
    return results;
}

}   // namespace pdf
