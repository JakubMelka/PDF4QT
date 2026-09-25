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

#ifndef PDFOCRSESSION_H
#define PDFOCRSESSION_H

#include "pdfglobal.h"
#include "pdfocrmodel.h"
#include "pdfocrproject.h"
#include "pdfocrconfiguration.h"

#include <QObject>
#include <QRegularExpression>

#include <map>
#include <optional>
#include <functional>

namespace pdf
{
class PDFDocument;

/// Session of the OCR work (chapter 8 and 12.2 of the OCR specification):
/// holds the configuration, the results, the corrections, the local
/// undo/redo history and the dirty state. Every editing operation is
/// one undo step. The session never modifies the PDF document.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRSession : public QObject
{
    Q_OBJECT

public:
    explicit PDFOCRSession(QObject* parent);
    virtual ~PDFOCRSession() override;

    // Document -----------------------------------------------------------

    /// Sets the document snapshot and its identity
    void setDocument(const PDFDocument* document, PDFOCRDocumentIdentity identity);
    const PDFDocument* getDocument() const { return m_document; }
    const PDFOCRDocumentIdentity& getDocumentIdentity() const { return m_identity; }

    // Configuration ------------------------------------------------------

    const PDFOCRConfiguration& getConfiguration() const { return m_configuration; }
    void setConfiguration(PDFOCRConfiguration configuration);

    const std::map<PDFInteger, PDFOCRPageOverride>& getPageOverrides() const { return m_pageOverrides; }
    std::optional<PDFOCRPageOverride> getPageOverride(PDFInteger pageIndex) const;
    void setPageOverride(PDFInteger pageIndex, PDFOCRPageOverride pageOverride);
    void clearPageOverride(PDFInteger pageIndex);

    /// Returns effective configuration of the page (profile -> job -> page)
    PDFOCRConfiguration getEffectiveConfiguration(PDFInteger pageIndex) const;

    double getReviewThreshold() const { return m_configuration.reviewThreshold; }

    /// Returns the criteria of the review: the score threshold, the dictionary
    /// criterion and the user words of the configuration (accepted words)
    PDFOCRReviewCriteria getReviewCriteria() const;

    // Results ------------------------------------------------------------

    /// Returns page result, or nullptr, if page has no record
    const PDFOCRPageResult* getPage(PDFInteger pageIndex) const;

    /// Returns page result, creating a pending record, if it does not exist
    PDFOCRPageResult& getOrCreatePage(PDFInteger pageIndex);

    /// Returns pages with any record
    std::vector<PDFInteger> getPages() const;

    /// Returns pages with valid result (Done or NoText)
    std::vector<PDFInteger> getPagesWithResults() const;

    /// Sets the result of the page (from the job). Regions of the existing
    /// record are preserved, if the result has none. The review-only flag of
    /// the result is kept as is. If the result has no raw recognition
    /// (originalBlocks), its blocks become the raw recognition (DATA-02).
    /// Not an undo step.
    void setPageResult(PDFOCRPageResult result);

    /// Marks the result of the page as recognized for the review and the export
    /// only (it must never be written into the PDF). The flag is a property of
    /// the result, not an undo step; it survives the editing, undo/redo and the
    /// project round trip (INPUT-04, EXPORT-03).
    void setPageReviewOnly(PDFInteger pageIndex, bool reviewOnly);

    /// Binds the results of the pages to a new revision of the pages, which changed
    /// without a change of the geometry (the scanned images were compressed when the
    /// text layer was written). A project saved afterwards matches the new revision.
    /// Not an undo step.
    void rebindPageFingerprints(const std::map<PDFInteger, QByteArray>& fingerprints);

    /// Finds the word of the raw recognition of the page by its identifier
    /// (the identifier at the time of the recognition), or nullptr (DATA-02)
    const PDFOCRWord* findOriginalWord(PDFInteger pageIndex, int wordId) const;

    /// Returns the words of the raw recognition of the page in reading order
    std::vector<const PDFOCRWord*> getOriginalWords(PDFInteger pageIndex) const;

    /// Sets the transient state of the page (Preparing, Recognizing). Not an undo step.
    void setPageState(PDFInteger pageIndex, PDFOCRPageState state);

    /// Marks results of the pages as stale (settings changed)
    void markPagesStale(const std::vector<PDFInteger>& pages);

    /// Removes the result of the page (keeps regions)
    void clearPageResult(PDFInteger pageIndex);

    /// Sets the blank detection override of the page
    void setBlankDetectionOverridden(PDFInteger pageIndex, bool overridden);

    // Regions (undo steps) -----------------------------------------------

    int addRegion(PDFInteger pageIndex, PDFOCRRegion region);
    bool updateRegion(PDFInteger pageIndex, const PDFOCRRegion& region);
    bool removeRegion(PDFInteger pageIndex, int regionId);

    /// Creates editable regions from the detected blocks of the page (REGION-02).
    /// Returns number of the created regions; single undo step.
    int createRegionsFromBlocks(PDFInteger pageIndex);

    // Corrections (undo steps, EDIT-01..EDIT-06) --------------------------

    bool setWordText(PDFInteger pageIndex, int wordId, const QString& text);
    bool setWordQuad(PDFInteger pageIndex, int wordId, const PDFOCRQuad& quad);
    bool setWordReviewState(PDFInteger pageIndex, int wordId, PDFOCRReviewState state);
    bool restoreOriginalText(PDFInteger pageIndex, int wordId);
    bool confirmAllWords(PDFInteger pageIndex, int* count);
    bool mergeWords(PDFInteger pageIndex, int firstWordId, int secondWordId, int* newWordId);
    bool splitWord(PDFInteger pageIndex, int wordId, int characterPosition, int* newWordId);
    bool insertWord(PDFInteger pageIndex, int lineId, int afterWordId, const QString& text, const PDFOCRQuad& quad, int* newWordId);
    bool insertLine(PDFInteger pageIndex, int blockId, const QString& text, const PDFOCRQuad& quad, int* newLineId);
    bool removeWord(PDFInteger pageIndex, int wordId);
    bool removeLine(PDFInteger pageIndex, int lineId);
    bool setLineText(PDFInteger pageIndex, int lineId, const QString& text);
    bool moveBlock(PDFInteger pageIndex, int blockId, int newIndex);
    bool moveLine(PDFInteger pageIndex, int lineId, int newIndex);
    bool setLineBaseline(PDFInteger pageIndex, int lineId, const QLineF& baseline);

    /// Merges two lines of the same block (EDIT-02): the words of the second line
    /// are appended after the words of the first line and keep their identifiers,
    /// the merged line gets a new identifier, its geometry is the oriented union
    /// of both lines and its baseline runs from the start of the first line to the
    /// end of the second line. Both lines must have the same text direction.
    bool mergeLines(PDFInteger pageIndex, int firstLineId, int secondLineId, int* newLineId);

    /// Splits the line after the given word (EDIT-02): the words after it are
    /// moved into a new line, which is inserted right after the original line.
    /// Geometries of both lines are recomputed from their words.
    bool splitLine(PDFInteger pageIndex, int lineId, int afterWordId, int* newLineId);

    /// Moves the line into another block at the given index (EDIT-02). An empty
    /// source block is removed, geometries of the blocks are recomputed from their
    /// lines. If the target block is the block of the line, the line is reordered.
    bool moveLineToBlock(PDFInteger pageIndex, int lineId, int targetBlockId, int newIndex);

    // Candidates (EDIT-07) -----------------------------------------------

    enum class CandidateMode
    {
        Keep,           ///< Keep the existing result, discard the candidate
        Replace,        ///< Replace the whole page result by the candidate
        ReplaceRegion   ///< Replace only the blocks of the region
    };

    /// Applies the candidate result of a repeated recognition. Existing manual
    /// corrections are never overwritten silently: the mode is chosen by the user.
    bool applyCandidate(PDFInteger pageIndex, const PDFOCRPageResult& candidate, CandidateMode mode, int regionId);

    /// Replaces the words of the line by the words of a repeated recognition
    /// of the line area (REGION-03). Single undo step.
    bool replaceLineWords(PDFInteger pageIndex, int lineId, const std::vector<PDFOCRWord>& words);

    /// Replaces a single word by the words of a repeated recognition of its area
    bool replaceWord(PDFInteger pageIndex, int wordId, const std::vector<PDFOCRWord>& words);

    /// Returns true, if the page has manual corrections
    bool hasManualCorrections(PDFInteger pageIndex) const;

    // Find and replace (EDIT-05) ------------------------------------------

    struct FindOptions
    {
        bool caseSensitive = false;
        bool wholeWords = false;
    };

    /// Hit of the search. The search runs over the text of the line (words joined
    /// by a single space, discarded words skipped), so a phrase spanning several
    /// words is found too. For a hit inside a single word, position and length
    /// are relative to the text of the word; for a hit touching several words
    /// (or reaching beyond the text of its single word), position is the offset
    /// in the text of the line.
    struct FindHit
    {
        PDFInteger pageIndex = -1;

        /// First word of the hit
        int wordId = 0;

        /// Line of the hit
        int lineId = 0;

        /// All words the hit touches, in reading order (at least one)
        std::vector<int> wordIds;

        int position = 0;
        int length = 0;

        /// True, if the hit lies inside the text of a single word (position and
        /// length are then relative to the text of the word)
        bool singleWord = false;
    };

    std::vector<FindHit> find(const std::vector<PDFInteger>& pages, const QString& text, const FindOptions& options) const;

    /// Replaces all occurrences in the pages. Returns number of replaced occurrences.
    /// The whole replacement is a single undo step. A hit inside a single word
    /// changes the text of the word; a hit spanning several words replaces the
    /// substring of the line text, and the line is tokenized again (as by
    /// setLineText, unchanged words keep their identity and geometry).
    int replaceAll(const std::vector<PDFInteger>& pages, const QString& text, const QString& replacement, const FindOptions& options);

    // Review navigation ----------------------------------------------------

    struct WordReference
    {
        PDFInteger pageIndex = -1;
        int wordId = 0;

        bool isValid() const { return pageIndex >= 0 && wordId > 0; }
    };

    /// Finds next/previous word requiring a review, starting after the given word
    std::optional<WordReference> findReviewItem(const std::vector<PDFInteger>& pages, const WordReference& start, bool forward) const;

    /// Returns all words requiring a review on the page
    std::vector<int> getReviewWords(PDFInteger pageIndex) const;

    // Statistics -----------------------------------------------------------

    PDFOCRConfidenceStatistics getStatistics(PDFInteger pageIndex) const;
    PDFOCRConfidenceStatistics getStatistics(const std::vector<PDFInteger>& pages) const;

    // Undo / redo --------------------------------------------------------

    bool canUndo() const { return !m_undoSteps.empty(); }
    bool canRedo() const { return !m_redoSteps.empty(); }
    QString getUndoText() const;
    QString getRedoText() const;
    void undo();
    void redo();
    void clearHistory();

    /// Removes the undo/redo steps, which contain a snapshot of the page. It is
    /// used when the page gets a new content outside of the editing history
    /// (new recognition, cleared result), so the undo cannot bring back an
    /// obsolete snapshot and throw the recognition away.
    void clearHistoryOfPage(PDFInteger pageIndex);

    // Dirty state --------------------------------------------------------

    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty);

    // Project ------------------------------------------------------------

    PDFOCRProject createProject(const std::vector<PDFInteger>& selectedPages) const;

    /// Loads the project. Pages of the project are imported; if requireMatch is true,
    /// only pages with matching fingerprint are imported.
    void loadProject(const PDFOCRProject& project, const std::vector<PDFInteger>& pages);

    /// Returns the page results (for export)
    std::vector<const PDFOCRPageResult*> getResults(const std::vector<PDFInteger>& pages) const;

signals:
    void pageChanged(qint64 pageIndex);
    void pagesChanged();
    void undoRedoChanged();
    void dirtyChanged(bool dirty);
    void configurationChanged();

private:
    struct UndoStep
    {
        QString text;
        std::vector<std::pair<PDFInteger, PDFOCRPageResult>> before;
        std::vector<std::pair<PDFInteger, PDFOCRPageResult>> after;
    };

    /// Returns true, if no page of the step is being processed
    bool isStepApplicable(const UndoStep& step) const;

    /// Helper for the undo steps: snapshots the pages, runs the edit and
    /// records the step, if the edit returned true.
    bool edit(const std::vector<PDFInteger>& pages, const QString& text, const std::function<bool()>& operation);
    void applyStep(const std::vector<std::pair<PDFInteger, PDFOCRPageResult>>& snapshot);
    void updateWordFlags(PDFOCRWord& word) const;
    PDFOCRPageResult* getEditablePage(PDFInteger pageIndex);

    /// Replaces the words of the line by the tokens of the text, unchanged tokens
    /// keep their geometry and identity (EDIT-03). Not an undo step by itself.
    bool applyLineText(PDFOCRPageResult* page, PDFOCRLine* line, const QString& text);

    /// Finds the hits of the expression in the text of the line (page index is not set)
    static std::vector<FindHit> findInLine(const PDFOCRLine& line, const QRegularExpression& expression);

    /// Returns the original text of the word for the restoration: the historical
    /// text of the word, or the concatenation of the original texts of its
    /// predecessors in the raw recognition. Empty, if not available.
    static QString getRestorableText(const PDFOCRPageResult& page, const PDFOCRWord& word);

    const PDFDocument* m_document = nullptr;
    PDFOCRDocumentIdentity m_identity;
    PDFOCRConfiguration m_configuration;
    std::map<PDFInteger, PDFOCRPageOverride> m_pageOverrides;
    std::map<PDFInteger, PDFOCRPageResult> m_pages;
    std::vector<UndoStep> m_undoSteps;
    std::vector<UndoStep> m_redoSteps;
    bool m_dirty = false;
    static constexpr size_t MaximumUndoSteps = 200;
};

}   // namespace pdf

#endif // PDFOCRSESSION_H
