// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFDIFF_UTILS_H
#define PDFDIFF_UTILS_H

#include "settings.h"
#include "pdfdiff.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdocumentdrawinterface.h"

namespace pdf
{
class PDFDocumentBuilder;
}   // namespace pdf

namespace pdfdiff
{

class ComparedDocumentMapper
{
public:

    enum class Mode
    {
        Left,
        Right,
        Combined,
        Overlay
    };

    void update(Mode mode,
                bool filterDifferences,
                const pdf::PDFDiffResult& diff,
                const pdf::PDFDocument* leftDocument,
                const pdf::PDFDocument* rightDocument,
                const pdf::PDFDocument* currentDocument);

    const pdf::PDFDrawSpaceController::LayoutItems& getLayout() const { return m_layout; }
    void setPageSequence(const pdf::PDFDiffResult::PageSequence& sequence) { m_pageSequence = sequence; }

    /// Returns left page index (that means page index in left document),
    /// if it doesn't exist, -1 is returned.
    /// \param pageIndex Actual page index
    pdf::PDFInteger getLeftPageIndex(pdf::PDFInteger pageIndex) const;

    /// Returns right page index (that means page index in right document),
    /// if it doesn't exist, -1 is returned.
    /// \param pageIndex Actual page index
    pdf::PDFInteger getRightPageIndex(pdf::PDFInteger pageIndex) const;

    /// Returns actual page index from left page index, or -1.
    /// \param leftPageIndex Left page index
    pdf::PDFInteger getPageIndexFromLeftPageIndex(pdf::PDFInteger leftPageIndex) const;

    /// Returns actual page index from right page index, or -1.
    /// \param rightPageIndex Right page index
    pdf::PDFInteger getPageIndexFromRightPageIndex(pdf::PDFInteger rightPageIndex) const;

private:
    pdf::PDFDrawSpaceController::LayoutItems m_layout;
    bool m_allLeft = false;
    bool m_allRight = false;
    std::map<pdf::PDFInteger, pdf::PDFInteger> m_leftPageIndices;
    std::map<pdf::PDFInteger, pdf::PDFInteger> m_rightPageIndices;
    pdf::PDFDiffResult::PageSequence m_pageSequence;
};

class DifferencesDrawInterface : public pdf::IDocumentDrawInterface
{
    Q_DECLARE_TR_FUNCTIONS(pdfdiff::DifferencesDrawInterface)

public:
    explicit DifferencesDrawInterface(const Settings* settings,
                                      const ComparedDocumentMapper* mapper,
                                      const pdf::PDFDiffResult* diffResult);

    virtual void drawPage(QPainter* painter, pdf::PDFInteger pageIndex,
                          const pdf::PDFPrecompiledPage* compiledPage,
                          pdf::PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const pdf::PDFColorConvertor& convertor,
                          QList<pdf::PDFRenderError>& errors) const override;

    virtual void drawPostRendering(QPainter* painter, QRect rect) const override;

    /// Draw annotations for differences
    /// \param document Document
    /// \param builder Builder
    void drawAnnotations(const pdf::PDFDocument* document, pdf::PDFDocumentBuilder* builder);

private:
    void drawRectangle(QPainter* painter,
                       const QTransform& pagePointToDevicePointMatrix,
                       const QRectF& rect,
                       QColor color) const;

    void drawMarker(QPainter* painter,
                    const QTransform& pagePointToDevicePointMatrix,
                    const QRectF& rect,
                    QColor color,
                    bool isLeft) const;

    QColor getColorForIndex(size_t index) const;

    const Settings* m_settings;
    const ComparedDocumentMapper* m_mapper;
    const pdf::PDFDiffResult* m_diffResult;
};

}   // namespace pdfdiff

#endif // PDFDIFF_UTILS_H
