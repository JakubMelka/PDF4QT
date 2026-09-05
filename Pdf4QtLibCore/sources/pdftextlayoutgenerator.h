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

#include "pdfpagecontentprocessor.h"

namespace pdf
{

class PDF4QTLIBCORESHARED_EXPORT PDFTextLayoutGenerator : public PDFPageContentProcessor
{
    using BaseClass = PDFPageContentProcessor;

public:
    explicit PDFTextLayoutGenerator(PDFRenderer::Features features,
                                    const PDFPage* page,
                                    const PDFDocument* document,
                                    const PDFFontCache* fontCache,
                                    const PDFCMS* cms,
                                    const PDFOptionalContentActivity* optionalContentActivity,
                                    QTransform pagePointToDevicePointMatrix,
                                    const PDFMeshQualitySettings& meshQualitySettings) :
        BaseClass(page, document, fontCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
        m_features(features)
    {

    }

    /// Creates text layout from the text
    PDFTextLayout createTextLayout();

protected:
    virtual bool isContentSuppressedByOC(PDFObjectReference ocgOrOcmd) override;
    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override;
    virtual void performMarkedContentBegin(const QByteArray& tag, const PDFObject& properties) override;
    virtual void performMarkedContentEnd() override;

private:
    /// Mirrors the marked-content nesting: every BDC pushes a span, every EMC
    /// pops one. When the span carries an /ActualText property, its text is
    /// the authoritative logical text of the glyphs in the span and repairs
    /// degraded ToUnicode mappings (ligatures collapsed to one UTF-16 unit,
    /// or mark glyphs duplicated on their base's cluster).
    struct ActualTextSpan
        {
            size_t startIndex = 0; ///< Layout character index at BDC time
            QString actualText;    ///< Decoded /ActualText (empty = no property)
            bool hasActualText = false; ///< Whether /ActualText was present
        };

    PDFRenderer::Features m_features;
    PDFTextLayout m_textLayout;
    std::vector<ActualTextSpan> m_actualTextSpans;

};

}   // namespace pdf
