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

#ifndef PDFDIFF_H
#define PDFDIFF_H

#include "pdfdocument.h"
#include "pdfprogress.h"
#include "pdfutils.h"
#include "pdfalgorithmlcs.h"
#include "pdfdocumenttextflow.h"
#include "pdfoperationcontrol.h"

#include <QObject>
#include <QFuture>
#include <QFutureWatcher>

#include <atomic>

class QIODevice;
class QXmlStreamWriter;

namespace pdf
{

struct PDFDiffPageContext;

class PDF4QTLIBCORESHARED_EXPORT PDFDiffResult
{
public:
    explicit PDFDiffResult();

    enum class Type : uint32_t
    {
        Invalid                         = 0x0000,
        PageMoved                       = 0x0001,
        PageAdded                       = 0x0002,
        PageRemoved                     = 0x0004,
        RemovedTextCharContent          = 0x0008,
        RemovedVectorGraphicContent     = 0x0010,
        RemovedImageContent             = 0x0020,
        RemovedShadingContent           = 0x0040,
        AddedTextCharContent            = 0x0080,
        AddedVectorGraphicContent       = 0x0100,
        AddedImageContent               = 0x0200,
        AddedShadingContent             = 0x0400,
        TextReplaced                    = 0x0800,
        TextAdded                       = 0x1000,
        TextRemoved                     = 0x2000,
    };

    struct PageSequenceItem
    {
        PDFInteger leftPage = -1;
        PDFInteger rightPage = -1;
    };

    using PageSequence = std::vector<PageSequenceItem>;

    using RectInfos = std::vector<std::pair<PDFInteger, QRectF>>;
    using RectInfosIt = typename RectInfos::const_iterator;

    void setResult(PDFOperationResult result) { m_result = std::move(result); }
    const PDFOperationResult& getResult() const { return m_result; }

    /// Returns true, if some difference was found
    bool isChanged() const { return getDifferencesCount() > 0; }

    /// Returns true, if no difference was found
    bool isSame() const { return !isChanged(); }

    /// Returns number of detected changes
    size_t getDifferencesCount() const { return m_differences.size(); }

    /// Returns message describing difference in a page content
    /// \param index Index
    QString getMessage(size_t index) const;

    /// Returns index of left page (or -1, if difference occured
    /// only on a right page)
    /// \param index Index
    PDFInteger getLeftPage(size_t index) const;

    /// Returns index of right page (or -1, if difference occured
    /// only on a left page)
    /// \param index Index
    PDFInteger getRightPage(size_t index) const;

    /// Return type of difference
    /// \param index Index
    Type getType(size_t index) const;

    /// Returns text description of type
    /// \param index Index
    QString getTypeDescription(size_t index) const;

    /// Returns text description of a type ("Text replaced"; an empty text for Invalid)
    /// \param type Type
    static QString getTypeDescription(Type type);

    /// The message of a difference without the text of the documents (getMessage without the
    /// texts: "Text on page 3 has been replaced by text on page 4."), for an output, which may
    /// not contain the content of a document without bit 5 (owner answer Q7.2 of step 7 of phase 2)
    /// \param index Index
    QString getDescription(size_t index) const;

    /// The text of the pages of the difference (a TextReplaced) was too large to be compared
    /// word by word (B191, R7-6): the texts are not in the difference, its rectangles cover the
    /// text of each page; getMessage says it
    /// \param index Index
    bool isTextTooLarge(size_t index) const;

    /// Returns the removed text of a text difference (TextRemoved, TextReplaced; empty otherwise)
    /// \param index Index
    QString getRemovedText(size_t index) const;

    /// Returns the added text of a text difference (TextAdded, TextReplaced; empty otherwise)
    /// \param index Index
    QString getAddedText(size_t index) const;

    /// Pages of the original document (indices from 0, sorted, unique), whose text was too large
    /// to be compared word by word (isTextTooLarge)
    std::vector<PDFInteger> getTextTooLargeLeftPageIndices() const;

    /// Returns iterator range for rectangles of "left" pages of an item
    std::pair<RectInfosIt, RectInfosIt> getLeftRectangles(size_t index) const;

    /// Returns iterator range for rectangles of "right" pages of an item
    std::pair<RectInfosIt, RectInfosIt> getRightRectangles(size_t index) const;

    bool isPageMoveAddRemoveDifference(size_t index) const;
    bool isPageMoveDifference(size_t index) const;
    bool isAddDifference(size_t index) const;
    bool isRemoveDifference(size_t index) const;
    bool isReplaceDifference(size_t index) const;

    bool hasPageMoveDifferences() const { return m_typeFlags & FLAGS_PAGE_MOVE; }
    bool hasTextDifferences() const { return m_typeFlags & FLAGS_TEXT; }
    bool hasVectorGraphicsDifferences() const { return m_typeFlags & FLAGS_VECTOR_GRAPHICS; }
    bool hasImageDifferences() const { return m_typeFlags & FLAGS_IMAGE; }
    bool hasShadingDifferences() const { return m_typeFlags & FLAGS_SHADING; }

    /// Returns sorted changed page indices from left document
    std::vector<PDFInteger> getChangedLeftPageIndices() const;

    /// Returns sorted changed page indices from right document
    std::vector<PDFInteger> getChangedRightPageIndices() const;

    /// Filters results using given critera
    /// \param filterPageMoveDifferences Filter page move differences?
    /// \param filterTextDifferences Filter text diffferences?
    /// \param filterVectorGraphicsDifferences Filter vector graphics differences?
    /// \param filterImageDifferences Filter image differences?
    /// \param filterShadingDifferences Filter shading differences?
    PDFDiffResult filter(bool filterPageMoveDifferences,
                         bool filterTextDifferences,
                         bool filterVectorGraphicsDifferences,
                         bool filterImageDifferences,
                         bool filterShadingDifferences) const;

    const PageSequence& getPageSequence() const;
    void setPageSequence(PageSequence pageSequence);

    /// Saves all differences to a XML stream
    /// represented by device. The element "difference" has the attributes "type", "left",
    /// "right" (pages from 1) and the elements "text-added", "text-removed" as before step 7c of
    /// phase 2; since then it has an element "rect" per rectangle (attributes "side" left / right,
    /// "page" from 1, "x", "y", "width", "height" in points of the page, C locale) and the
    /// attribute "text-too-large" for a text, which was not compared word by word - a reader of
    /// the old form, which skips unknown elements and attributes, reads it as before.
    /// \param device Output device
    void saveToXML(QIODevice* device) const;

    /// Saves all differences to a byte array
    /// \param byteArray Output byte array
    void saveToXML(QByteArray* byteArray) const;

    /// Saves all differences to a string
    /// \param string Output string
    void saveToXML(QString* string) const;

private:
    friend class PDFDiff;
    friend class PDFDiffHelper;

    static constexpr uint32_t FLAGS_PAGE_MOVE = uint32_t(Type::PageMoved) | uint32_t(Type::PageAdded) | uint32_t(Type::PageRemoved);
    static constexpr uint32_t FLAGS_TEXT = uint32_t(Type::RemovedTextCharContent) | uint32_t(Type::AddedTextCharContent) | uint32_t(Type::TextReplaced) | uint32_t(Type::TextAdded) | uint32_t(Type::TextRemoved);
    static constexpr uint32_t FLAGS_VECTOR_GRAPHICS = uint32_t(Type::RemovedVectorGraphicContent) | uint32_t(Type::AddedVectorGraphicContent);
    static constexpr uint32_t FLAGS_IMAGE = uint32_t(Type::RemovedImageContent) | uint32_t(Type::AddedImageContent);
    static constexpr uint32_t FLAGS_SHADING = uint32_t(Type::RemovedShadingContent) | uint32_t(Type::AddedShadingContent);

    static constexpr uint32_t FLAGS_TYPE_PAGE_MOVE = uint32_t(Type::PageMoved);
    static constexpr uint32_t FLAGS_TYPE_PAGE_MOVE_ADD_REMOVE = uint32_t(Type::PageMoved) | uint32_t(Type::PageAdded) | uint32_t(Type::PageRemoved);
    static constexpr uint32_t FLAGS_TYPE_ADD = uint32_t(Type::PageAdded) | uint32_t(Type::AddedTextCharContent) | uint32_t(Type::AddedVectorGraphicContent) | uint32_t(Type::AddedImageContent) | uint32_t(Type::AddedShadingContent) | uint32_t(Type::TextAdded);
    static constexpr uint32_t FLAGS_TYPE_REMOVE = uint32_t(Type::PageRemoved) | uint32_t(Type::RemovedTextCharContent) | uint32_t(Type::RemovedVectorGraphicContent) | uint32_t(Type::RemovedImageContent) | uint32_t(Type::RemovedShadingContent) | uint32_t(Type::TextRemoved);
    static constexpr uint32_t FLAGS_TYPE_REPLACE = uint32_t(Type::TextReplaced);

    void addPageMoved(PDFInteger pageIndex1, PDFInteger pageIndex2);
    void addPageAdded(PDFInteger pageIndex);
    void addPageRemoved(PDFInteger pageIndex);

    void addRemovedTextCharContent(PDFInteger pageIndex, QRectF rect);
    void addRemovedVectorGraphicContent(PDFInteger pageIndex, QRectF rect);
    void addRemovedImageContent(PDFInteger pageIndex, QRectF rect);
    void addRemovedShadingContent(PDFInteger pageIndex, QRectF rect);
    void addAddedTextCharContent(PDFInteger pageIndex, QRectF rect);
    void addAddedVectorGraphicContent(PDFInteger pageIndex, QRectF rect);
    void addAddedImageContent(PDFInteger pageIndex, QRectF rect);
    void addAddedShadingContent(PDFInteger pageIndex, QRectF rect);

    void addTextAdded(PDFInteger pageIndex, QString text, const RectInfos& rectInfos);
    void addTextRemoved(PDFInteger pageIndex, QString text, const RectInfos& rectInfos);

    void addTextReplaced(PDFInteger pageIndex1,
                         PDFInteger pageIndex2,
                         QString textRemoved,
                         QString textAdded,
                         const RectInfos& rectInfos1,
                         const RectInfos& rectInfos2);

    /// A TextReplaced of a pair of pages, whose text was too large to be compared word by word
    void addTextTooLarge(PDFInteger pageIndex1,
                         PDFInteger pageIndex2,
                         const RectInfos& rectInfos1,
                         const RectInfos& rectInfos2);

    void saveToStream(QXmlStreamWriter* stream) const;

    /// The type flags of the differences (after a change of the list)
    void updateTypeFlags();

    void finalize();

    uint32_t getTypeFlags(size_t index) const;

    /// Single content difference descriptor. It describes type
    /// of difference (such as graphics, image, text change) on a page
    /// or on a list of multiple pages.
    struct Difference
    {
        Type type = Type::Invalid;
        PDFInteger pageIndex1 = -1;
        PDFInteger pageIndex2 = -1;
        size_t leftRectIndex = 0;
        size_t leftRectCount = 0;
        size_t rightRectIndex = 0;
        size_t rightRectCount = 0;
        int textAddedIndex = -1;
        int textRemovedIndex = -1;
        bool isTextTooLarge = false;
    };

    using Differences = std::vector<Difference>;

    void addLeftItem(Type type, PDFInteger pageIndex, QRectF rect);
    void addRightItem(Type type, PDFInteger pageIndex, QRectF rect);

    void addRectLeft(Difference& difference, QRectF rect);
    void addRectRight(Difference& difference, QRectF rect);

    Differences m_differences;
    RectInfos m_rects; ///< Rectangles with page indices
    PDFOperationResult m_result;
    QStringList m_strings;
    uint32_t m_typeFlags = 0;
    PageSequence m_pageSequence;
};

/// Class for result navigation, can go to next, or previous result.
class PDF4QTLIBCORESHARED_EXPORT PDFDiffResultNavigator : public QObject
{
    Q_OBJECT

public:
    explicit PDFDiffResultNavigator(QObject* parent);
    virtual ~PDFDiffResultNavigator() override;

    void setResult(const PDFDiffResult* diffResult);

    /// Returns true, if valid result is selected
    bool isSelected() const;

    /// Returns true if action go to next result can be performed,
    /// otherwise false is returned.
    bool canGoNext() const;

    /// Returns true if action go to previous result can be performed,
    /// otherwise false is returned.
    bool canGoPrevious() const;

    /// Goes to next result. If action cannot be performed,
    /// nothing happens and signal is not emitted.
    void goNext();

    /// Goes to previous result. If action cannot be performed,
    /// nothing happens and signal is not emitted.
    void goPrevious();

    /// Updates selection, if difference result was changed
    void update();

    /// Selects current index
    /// \param currentIndex
    void select(size_t currentIndex);

signals:
    void selectionChanged(size_t currentIndex);

private:
    size_t getLimit() const { return m_diffResult ? m_diffResult->getDifferencesCount() : 0; }

    const PDFDiffResult* m_diffResult;
    size_t m_currentIndex;
};

/// Diff engine for comparing two pdf documents.
class PDF4QTLIBCORESHARED_EXPORT PDFDiff : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFDiff(QObject* parent);
    virtual ~PDFDiff() override;

    enum Option
    {
        None                    = 0x0000,
        Asynchronous            = 0x0001,   ///< Compare document asynchronously
        PC_Text                 = 0x0002,   ///< Use text to compare pages (determine, which pages correspond to each other)
        PC_VectorGraphics       = 0x0004,   ///< Use vector graphics to compare pages (determine, which pages correspond to each other)
        PC_Images               = 0x0008,   ///< Use images to compare pages (determine, which pages correspond to each other)
        PC_Mesh                 = 0x0010,   ///< Use mesh to compare pages (determine, which pages correspond to each other)
        CompareTextsAsVector    = 0x0020,   ///< Compare texts as vector graphics
        CompareWords            = 0x0040,   ///< Compare words, not just characters
    };
    Q_DECLARE_FLAGS(Options, Option)

    /// Source document (left)
    /// \param leftDocument Document
    void setLeftDocument(const PDFDocument* leftDocument);

    /// Source document (right)(
    /// \param rightDocument Document
    void setRightDocument(const PDFDocument* rightDocument);

    /// Source pages to be compared (left document)
    /// \param pagesForLeftDocument Page indices
    void setPagesForLeftDocument(PDFClosedIntervalSet pagesForLeftDocument);

    /// Source pages to be compared (right document)
    /// \param pagesForRightDocument Page indices
    void setPagesForRightDocument(PDFClosedIntervalSet pagesForRightDocument);

    /// Sets progress object
    /// \param progress Progress object
    void setProgress(PDFProgressSink* progress) { m_progress = progress; }

    /// Enables or disables comparator engine option
    /// \param option Option
    /// \param enable Enable or disable option?
    void setOption(Option option, bool enable) { m_options.setFlag(option, enable); }

    /// Starts comparator engine. If asynchronous engine option
    /// is enabled, then separate thread is started, in which two
    /// document is compared, and then signal \p comparationFinished,
    /// otherwise this function is blocking until comparation process
    /// is finished. The old Diff application uses it (a thin wrapper of
    /// compare since step 7c of phase 2, R7-15).
    void start();

    /// The synchronous comparison of two documents in the thread of the caller (plan of step 7 of phase 2,
    /// R7-15: the tool Compare runs it in the job of its runner, never in a nested job; PDFDocumentToolCompare,
    /// PdfTool diff). The documents are immutable snapshots of the caller, read only. The page sets hold
    /// indices from 0; an empty set or an index outside of its document is an error of the result (checked
    /// before PDFClosedIntervalSet::unfold - B174, B192), never an exception; a document without pages is an
    /// error. The cancellation is checked in every long loop (the compilation of the pages, the text flow, the
    /// LCS - B193); a canceled comparison returns at once, the caller sees it by
    /// PDFOperationControl::isOperationCanceled and never reports it as "no differences" (B173). The text is
    /// compared within the budget of the LCS (B191, R7-6). Option Asynchronous is ignored.
    /// \param leftDocument The original document
    /// \param rightDocument The revised document
    /// \param leftPages Pages of the original document (indices from 0)
    /// \param rightPages Pages of the revised document (indices from 0)
    /// \param options Options of the comparison (PC_*, CompareTextsAsVector, CompareWords)
    /// \param control Cancellation (optional)
    /// \param progress Progress (optional; PDFProgressSink of the run, called by the threads of the comparison)
    static PDFDiffResult compare(const PDFDocument* leftDocument,
                                 const PDFDocument* rightDocument,
                                 const PDFClosedIntervalSet& leftPages,
                                 const PDFClosedIntervalSet& rightPages,
                                 Options options,
                                 const PDFOperationControl* control,
                                 PDFProgressSink* progress);

    /// Stops comparator engine. Result data are cleared.
    void stop();

    /// Returns result of a comparation process
    const PDFDiffResult& getResult() const { return m_result; }

    PDFDocumentTextFlowFactory::Algorithm getTextAnalysisAlgorithm() const;
    void setTextAnalysisAlgorithm(PDFDocumentTextFlowFactory::Algorithm textAnalysisAlgorithm);

signals:
    void comparationFinished();

private:

    enum Steps
    {
        StepExtractContentLeftDocument,
        StepExtractContentRightDocument,
        StepMatchPages,
        StepExtractTextLeftDocument,
        StepExtractTextRightDocument,
        StepCompare,
        StepLast
    };

    /// The cancellation of start / stop (the old Diff): a flag read by the threads of the comparison
    class CancelControl : public PDFOperationControl
    {
    public:
        virtual bool isOperationCanceled() const override { return canceled.load(); }

        std::atomic_bool canceled = false;
    };

    PDFDiffResult perform();
    void stepProgress();
    bool isCanceled() const { return PDFOperationControl::isOperationCanceled(m_control); }
    void performSteps(const std::vector<PDFInteger>& leftPages,
                      const std::vector<PDFInteger>& rightPages,
                      PDFDiffResult& result);
    void performPageMatching(const std::vector<PDFDiffPageContext>& leftPreparedPages,
                             const std::vector<PDFDiffPageContext>& rightPreparedPages,
                             PDFAlgorithmLongestCommonSubsequenceBase::Sequence& pageSequence,
                             std::map<size_t, size_t>& pageMatches);

    /// The pages of the unmatched positions, whose content is equal (a moved page): for each
    /// position of \p leftUnmatched the positions of \p rightUnmatched (B194, B195: no map written
    /// by the threads, a deterministic budget of the work)
    std::vector<std::vector<size_t>> findEqualPages(const std::vector<PDFDiffPageContext>& leftPreparedPages,
                                                    const std::vector<PDFDiffPageContext>& rightPreparedPages,
                                                    const std::vector<size_t>& leftUnmatched,
                                                    const std::vector<size_t>& rightUnmatched) const;
    void performCompare(const std::vector<PDFDiffPageContext>& leftPreparedPages,
                        const std::vector<PDFDiffPageContext>& rightPreparedPages,
                        PDFAlgorithmLongestCommonSubsequenceBase::Sequence& pageSequence,
                        const std::map<size_t, size_t>& pageMatches,
                        PDFDiffResult& result);
    void finalizeGraphicsPieces(PDFDiffPageContext& context);

    void onComparationPerformed();

    /// Calculates real epsilon for a page. Epsilon is used in page
    /// comparation process, where points closer that epsilon
    /// are recognized as equal.
    /// \param page Page
    PDFReal calculateEpsilonForPage(const PDFPage* page) const;

    PDFProgressSink* m_progress;
    const PDFDocument* m_leftDocument;
    const PDFDocument* m_rightDocument;
    PDFClosedIntervalSet m_pagesForLeftDocument;
    PDFClosedIntervalSet m_pagesForRightDocument;
    Options m_options;
    PDFReal m_epsilon;
    CancelControl m_cancelControl;                  ///< Cancellation of start / stop
    const PDFOperationControl* m_control = nullptr; ///< Cancellation of the running comparison (m_cancelControl of start, the control of compare)
    PDFDiffResult m_result;
    PDFDocumentTextFlowFactory::Algorithm m_textAnalysisAlgorithm;

    QFuture<PDFDiffResult> m_future;
    std::optional<QFutureWatcher<PDFDiffResult>> m_futureWatcher;
};

}   // namespace pdf

#endif // PDFDIFF_H
