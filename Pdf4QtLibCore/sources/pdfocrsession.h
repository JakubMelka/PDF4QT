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

#include <map>
#include <optional>

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
    /// record are preserved, if the result has none. Not an undo step.
    void setPageResult(PDFOCRPageResult result);

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

    struct FindHit
    {
        PDFInteger pageIndex = -1;
        int wordId = 0;
        int position = 0;
        int length = 0;
    };

    std::vector<FindHit> find(const std::vector<PDFInteger>& pages, const QString& text, const FindOptions& options) const;

    /// Replaces all occurrences in the pages. Returns number of replaced occurrences.
    /// The whole replacement is a single undo step.
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

    /// Helper for the undo steps: snapshots the pages, runs the edit and
    /// records the step, if the edit returned true.
    bool edit(const std::vector<PDFInteger>& pages, const QString& text, const std::function<bool()>& operation);
    void applyStep(const std::vector<std::pair<PDFInteger, PDFOCRPageResult>>& snapshot);
    void updateWordFlags(PDFOCRWord& word) const;
    PDFOCRPageResult* getEditablePage(PDFInteger pageIndex);

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
