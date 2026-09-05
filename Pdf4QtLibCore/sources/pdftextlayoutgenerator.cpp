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

#include "pdftextlayoutgenerator.h"
#include "pdfdocument.h"
#include "pdfencoding.h"
#include "pdfdbgheap.h"

namespace pdf
{

PDFTextLayout PDFTextLayoutGenerator::createTextLayout()
{
    m_textLayout.perform();
    m_textLayout.optimize();
    return qMove(m_textLayout);
}

bool PDFTextLayoutGenerator::isContentSuppressedByOC(PDFObjectReference ocgOrOcmd)
{
    if (m_features.testFlag(PDFRenderer::IgnoreOptionalContent))
    {
        return false;
    }

    return PDFPageContentProcessor::isContentSuppressedByOC(ocgOrOcmd);
}

bool PDFTextLayoutGenerator::isContentKindSuppressed(ContentKind kind) const
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

        case ContentKind::Forms:
            return false; // Forms can have text

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    return false;
}

void PDFTextLayoutGenerator::performOutputCharacter(const PDFTextCharacterInfo& info)
{
    if (!isContentSuppressed() && !info.character.isSpace())
    {
        m_textLayout.addCharacter(info);
    }
}

void PDFTextLayoutGenerator::performMarkedContentBegin(const QByteArray& tag, const PDFObject& properties)
{
    Q_UNUSED(tag);

    ActualTextSpan span;
    span.startIndex = m_textLayout.getCharacterCount();

    // The properties operand of BDC is the marked-content property dictionary.
    // It can be an inline dictionary (e.g., /Span << /ActualText <FEFF...> >> BDC)
    // or a name to be looked up in the page's /Properties resource dictionary.
    const PDFDictionary* dictionary = getDocument()->getDictionaryFromObject(properties);
    if (!dictionary && properties.isName())
    {
        if (const PDFDictionary* pageProperties = getPropertiesDictionary())
        {
            const PDFObject& resolved = pageProperties->get(properties.getString());
            dictionary = getDocument()->getDictionaryFromObject(resolved);
        }
    }

    if (dictionary && dictionary->hasKey("ActualText"))
        {
            const PDFObject& actualTextObject = getDocument()->getObject(dictionary->get("ActualText"));
            if (actualTextObject.isString())
            {
                span.actualText = PDFEncoding::convertTextString(actualTextObject.getString());
                span.hasActualText = true;
            }
        }

    m_actualTextSpans.push_back(std::move(span));
}

void PDFTextLayoutGenerator::performMarkedContentEnd()
{
    if (m_actualTextSpans.empty())
    {
        return;
    }

    ActualTextSpan span = std::move(m_actualTextSpans.back());
    m_actualTextSpans.pop_back();

    // Per PDF spec 14.9.4, an empty /ActualText means the sequence has no
        // text content: remove those characters from the extracted output.
        // But only if /ActualText was actually present (hasActualText).
        if (span.hasActualText && span.actualText.isEmpty())
        {
            const size_t glyphCount = m_textLayout.getCharacterCount() - span.startIndex;
            if (glyphCount > 0)
            {
                m_textLayout.replaceCharacters(span.startIndex, glyphCount, QString());
            }
            return;
        }

        // If /ActualText was not present, this is a no-op.
        if (!span.hasActualText)
        {
            return;
        }

    // The producing writer is assumed to emit /ActualText in visual order,
    // matching the glyph order of the layout. Replace the layout range with
    // the /ActualText payload: this repairs ligatures the ToUnicode CMap
    // degraded to one UTF-16 unit and deduplicates mark glyphs that share
    // their base's cluster. PDFTextLayout::replaceCharacters reuses each
    // original glyph slot's geometry, so docstrum line detection and the
    // per-character bounding boxes stay aligned.
    const size_t glyphCount = m_textLayout.getCharacterCount() - span.startIndex;
    if (glyphCount > 0)
    {
        m_textLayout.replaceCharacters(span.startIndex, glyphCount, span.actualText);
    }
}

}   // namespace pdf
