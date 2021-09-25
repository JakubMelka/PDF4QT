//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFDIFF_H
#define PDFDIFF_H

#include "pdfdocument.h"
#include "pdfprogress.h"
#include "pdfutils.h"
#include "pdfalgorithmlcs.h"
#include "pdfdocumenttextflow.h"

#include <QObject>
#include <QFuture>
#include <QFutureWatcher>

#include <atomic>

namespace pdf
{

struct PDFDiffPageContext;

class PDFDiffResult
{
public:
    explicit PDFDiffResult();

    enum class Type
    {
        Invalid,
        PageMoved,
        PageAdded,
        PageRemoved,
        RemovedTextCharContent,
        RemovedVectorGraphicContent,
        RemovedImageContent,
        RemovedShadingContent,
        AddedTextCharContent,
        AddedVectorGraphicContent,
        AddedImageContent,
        AddedShadingContent,
    };

    struct Difference
    {
        Type type = Type::Invalid;
        PDFInteger pageIndex1 = -1;
        PDFInteger pageIndex2 = -1;
        size_t leftRectIndex = 0;
        size_t leftRectCount = 0;
        size_t rightRectIndex = 0;
        size_t rightRectCount = 0;
    };

    using Differences = std::vector<Difference>;

    void setResult(PDFOperationResult result) { m_result = std::move(result); }
    const PDFOperationResult& getResult() const { return m_result; }

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

    QString getMessage(size_t index) const;

private:
    void addLeftItem(Type type, PDFInteger pageIndex, QRectF rect);
    void addRightItem(Type type, PDFInteger pageIndex, QRectF rect);

    void addRectLeft(Difference& difference, QRectF rect);
    void addRectRight(Difference& difference, QRectF rect);

    Differences m_differences;
    std::vector<QRectF> m_rects;
    PDFOperationResult m_result;
};

/// Diff engine for comparing two pdf documents.
class PDF4QTLIBSHARED_EXPORT PDFDiff : public QObject
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
    void setProgress(PDFProgress* progress) { m_progress = progress; }

    /// Enables or disables comparator engine option
    /// \param option Option
    /// \param enable Enable or disable option?
    void setOption(Option option, bool enable) { m_options.setFlag(option, enable); }

    /// Starts comparator engine. If asynchronous engine option
    /// is enabled, then separate thread is started, in which two
    /// document is compared, and then signal \p comparationFinished,
    /// otherwise this function is blocking until comparation process
    /// is finished.
    void start();

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

    PDFDiffResult perform();
    void stepProgress();
    void performSteps(const std::vector<PDFInteger>& leftPages,
                      const std::vector<PDFInteger>& rightPages);
    void performPageMatching(const std::vector<PDFDiffPageContext>& leftPreparedPages,
                             const std::vector<PDFDiffPageContext>& rightPreparedPages,
                             PDFAlgorithmLongestCommonSubsequenceBase::Sequence& pageSequence,
                             std::map<size_t, size_t>& pageMatches);
    void performCompare(const std::vector<PDFDiffPageContext>& leftPreparedPages,
                        const std::vector<PDFDiffPageContext>& rightPreparedPages,
                        PDFAlgorithmLongestCommonSubsequenceBase::Sequence& pageSequence,
                        const std::map<size_t, size_t>& pageMatches);
    void finalizeGraphicsPieces(PDFDiffPageContext& context);

    void onComparationPerformed();

    /// Calculates real epsilon for a page. Epsilon is used in page
    /// comparation process, where points closer that epsilon
    /// are recognized as equal.
    /// \param page Page
    PDFReal calculateEpsilonForPage(const PDFPage* page) const;

    PDFProgress* m_progress;
    const PDFDocument* m_leftDocument;
    const PDFDocument* m_rightDocument;
    PDFClosedIntervalSet m_pagesForLeftDocument;
    PDFClosedIntervalSet m_pagesForRightDocument;
    Options m_options;
    PDFReal m_epsilon;
    std::atomic_bool m_cancelled;
    PDFDiffResult m_result;
    PDFDocumentTextFlowFactory::Algorithm m_textAnalysisAlgorithm;

    QFuture<PDFDiffResult> m_future;
    std::optional<QFutureWatcher<PDFDiffResult>> m_futureWatcher;
};

}   // namespace pdf

#endif // PDFDIFF_H
