//    Copyright (C) 2021-2024 Jakub Melka
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
