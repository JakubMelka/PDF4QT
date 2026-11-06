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

#include "pdfdocumenttextexporter.h"

#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdfexecutionpolicy.h"
#include "pdffont.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfprogress.h"
#include "pdftextlayoutgenerator.h"

#include <QMutex>
#include <QStringEncoder>

#include <algorithm>
#include <atomic>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

#include "pdfdbgheap.h"

namespace pdf
{

/// Cancellation of the walk of one page: the run is canceled, or the page has shown its first
/// character (the rest of the page is not needed)
class PDFDocumentTextExporterPageControl : public PDFOperationControl
{
public:
    explicit PDFDocumentTextExporterPageControl(const PDFOperationControl* parent) :
        m_parent(parent)
    {

    }

    virtual bool isOperationCancelled() const override
    {
        return m_isTextFound.load(std::memory_order_relaxed) || PDFOperationControl::isOperationCancelled(m_parent);
    }

    void setTextFound() { m_isTextFound.store(true, std::memory_order_relaxed); }
    bool isTextFound() const { return m_isTextFound.load(std::memory_order_relaxed); }

private:
    const PDFOperationControl* m_parent;
    std::atomic<bool> m_isTextFound = false;
};

/// The layout generator, which only says, whether the page draws a character: it uses the
/// same suppression of the content kinds and of the optional content as the layout algorithm
class PDFDocumentTextExporterPageScanner : public PDFTextLayoutGenerator
{
public:
    using PDFTextLayoutGenerator::PDFTextLayoutGenerator;

    void setPageControl(PDFDocumentTextExporterPageControl* control)
    {
        m_pageControl = control;
        setOperationControl(control);
    }

protected:
    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override
    {
        if (m_pageControl && !info.character.isSpace() && !info.character.isNull())
        {
            m_pageControl->setTextFound();
        }
    }

private:
    PDFDocumentTextExporterPageControl* m_pageControl = nullptr;
};

/// Helpers of the exporter
class PDFDocumentTextExporterHelper
{
public:
    PDFDocumentTextExporterHelper() = delete;

    /// The text of a markup file: the characters, which XML 1.0 does not allow, are U+FFFD
    static QString sanitizeMarkup(const QString& text)
    {
        QString result;
        result.reserve(text.size());
        const qsizetype size = text.size();
        for (qsizetype i = 0; i < size; ++i)
        {
            const QChar character = text.at(i);
            if (character.isHighSurrogate() && i + 1 < size && text.at(i + 1).isLowSurrogate())
            {
                result.append(character);
                result.append(text.at(i + 1));
                ++i;
                continue;
            }

            const char16_t code = character.unicode();
            const bool isAllowed = (code >= 0x20 || code == 0x09 || code == 0x0A || code == 0x0D) && !character.isSurrogate() && code != 0xFFFE && code != 0xFFFF;
            result.append(isAllowed ? character : QChar(QChar::ReplacementCharacter));
        }
        return result;
    }

    /// The text without white space at the ends of its lines
    static QString trimLineEnds(const QString& text)
    {
        QStringList lines = text.split(QLatin1Char('\n'));
        for (QString& line : lines)
        {
            qsizetype size = line.size();
            while (size > 0 && line.at(size - 1).isSpace())
            {
                --size;
            }
            line.truncate(size);
        }
        return lines.join(QLatin1Char('\n'));
    }

    /// Number of the words of a text (runs of other characters than white space)
    static qint64 countWords(const QString& text)
    {
        qint64 words = 0;
        bool isInWord = false;
        for (const QChar character : text)
        {
            const bool isSpace = character.isSpace();
            if (!isSpace && !isInWord)
            {
                ++words;
            }
            isInWord = !isSpace;
        }
        return words;
    }

    /// The text has another character than white space
    static bool hasVisibleText(const QString& text)
    {
        return std::any_of(text.cbegin(), text.cend(), [](QChar character) { return !character.isSpace(); });
    }
};

void PDFDocumentTextExporter::write(const PDFDocumentTextFlow& flow, const Options& options, PDFOutputFormatter& formatter)
{
    // The loop of fetch-text (PdfTool): its output stays byte-identical for the default options
    QString documentTitle = options.documentTitle;
    if (options.sanitizeMarkup)
    {
        documentTitle = PDFDocumentTextExporterHelper::sanitizeMarkup(documentTitle);
    }

    formatter.beginDocument("text-extraction", documentTitle);
    if (!options.separateItems)
    {
        formatter.endl();
    }

    for (const PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        QString text = options.sanitizeMarkup ? PDFDocumentTextExporterHelper::sanitizeMarkup(item.text) : item.text;
        if (options.trimLineEnds)
        {
            text = PDFDocumentTextExporterHelper::trimLineEnds(text);
        }
        if (item.flags.testFlag(PDFDocumentTextFlow::StructureItemStart))
        {
            formatter.beginHeader("item", text);
        }

        if (isItemShown(item, options))
        {
            formatter.writeText("text", text);
            if (options.separateItems)
            {
                formatter.endl();
            }
        }

        if (item.flags.testFlag(PDFDocumentTextFlow::StructureItemEnd))
        {
            formatter.endHeader();
        }

        if (item.flags.testFlag(PDFDocumentTextFlow::PageEnd) && !options.separateItems)
        {
            formatter.endl();
        }
    }

    formatter.endDocument();
}

bool PDFDocumentTextExporter::isItemShown(const PDFDocumentTextFlow::Item& item, const Options& options)
{
    if (item.text.isEmpty())
    {
        return false;
    }

    return (item.flags.testFlag(PDFDocumentTextFlow::Text)) ||
           (item.flags.testFlag(PDFDocumentTextFlow::PageStart) && options.showPageNumbers) ||
           (item.flags.testFlag(PDFDocumentTextFlow::PageEnd) && options.showPageNumbers) ||
           (item.flags.testFlag(PDFDocumentTextFlow::StructureTitle) && options.showStructureTitles) ||
           (item.flags.testFlag(PDFDocumentTextFlow::StructureLanguage) && options.showStructureLanguage) ||
           (item.flags.testFlag(PDFDocumentTextFlow::StructureAlternativeDescription) && options.showStructureAlternativeDescription) ||
           (item.flags.testFlag(PDFDocumentTextFlow::StructureExpandedForm) && options.showStructureExpandedForm) ||
           (item.flags.testFlag(PDFDocumentTextFlow::StructureActualText) && options.showStructureActualText) ||
           (item.flags.testFlag(PDFDocumentTextFlow::StructurePhoneme) && options.showStructurePhoneme);
}

PDFDocumentTextExporter::TextCount PDFDocumentTextExporter::count(const PDFDocumentTextFlow& flow, const Options& options, PDFInteger pageIndex)
{
    using Helper = PDFDocumentTextExporterHelper;

    TextCount result;
    for (const PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        if (pageIndex >= 0 && item.pageIndex != pageIndex)
        {
            continue;
        }

        if (isItemShown(item, options))
        {
            // The characters, which write writes
            const QString text = options.trimLineEnds ? Helper::trimLineEnds(item.text) : item.text;
            result.words += Helper::countWords(text);
            result.characters += text.size();

            // The line "Page N" is no text of the page
            const bool isPageNumber = item.flags.testFlag(PDFDocumentTextFlow::PageStart) || item.flags.testFlag(PDFDocumentTextFlow::PageEnd);
            if (!isPageNumber && Helper::hasVisibleText(item.text))
            {
                result.hasText = true;
            }
        }

        if (item.flags.testFlag(PDFDocumentTextFlow::Text) && item.pageIndex >= 0 && Helper::hasVisibleText(item.text))
        {
            result.pagesWithText.push_back(item.pageIndex);
        }
    }

    std::sort(result.pagesWithText.begin(), result.pagesWithText.end());
    result.pagesWithText.erase(std::unique(result.pagesWithText.begin(), result.pagesWithText.end()), result.pagesWithText.end());
    return result;
}

PDFDocumentTextExporter::EncodedText PDFDocumentTextExporter::encode(QString text, PDFOutputFormatter::Style style, Encoding encoding)
{
    QString charset;
    QStringConverter::Encoding converterEncoding = QStringConverter::Utf8;
    switch (encoding)
    {
        case Encoding::Utf8:
            charset = QStringLiteral("UTF-8");
            break;

        case Encoding::Latin1:
            charset = QStringLiteral("ISO-8859-1");
            converterEncoding = QStringConverter::Latin1;
            break;

        case Encoding::System:
            charset = getSystemCharset();
            converterEncoding = QStringConverter::System;
            break;
    }

    const bool isUtf8 = charset.compare(QLatin1String("UTF-8"), Qt::CaseInsensitive) == 0;

    // A markup file says its charset (QXmlStreamWriter writes into a string without one)
    switch (style)
    {
        case PDFOutputFormatter::Style::Xml:
        {
            const QString declaration = QStringLiteral("<?xml version=\"1.0\"?>");
            if (!isUtf8 && text.startsWith(declaration))
            {
                text.replace(0, declaration.size(), QStringLiteral("<?xml version=\"1.0\" encoding=\"%1\"?>").arg(charset));
            }
            break;
        }

        case PDFOutputFormatter::Style::Html:
        {
            // The first <head> is the element of the formatter: a text of the document is escaped
            const qsizetype headIndex = text.indexOf(QLatin1String("<head>"));
            if (headIndex >= 0)
            {
                text.insert(headIndex + 6, QStringLiteral("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=%1\"/>").arg(charset));
            }
            break;
        }

        case PDFOutputFormatter::Style::Text:
            break;
    }

    EncodedText result;
    QStringEncoder encoder(converterEncoding);
    result.data = encoder.encode(text);

    if (!isUtf8)
    {
        // A character, which the encoding has not, is a question mark of the encoder: the
        // characters, which do not come back, are counted
        QStringDecoder decoder(converterEncoding);
        const QString decoded = decoder.decode(result.data);
        const qsizetype size = qMin(decoded.size(), text.size());
        for (qsizetype i = 0; i < size; ++i)
        {
            result.lostCharacters += decoded.at(i) != text.at(i) ? 1 : 0;
        }
        result.lostCharacters += qAbs(decoded.size() - text.size());
    }

    QString type;
    switch (style)
    {
        case PDFOutputFormatter::Style::Text:
            type = QStringLiteral("text/plain");
            break;
        case PDFOutputFormatter::Style::Xml:
            type = QStringLiteral("text/xml");
            break;
        case PDFOutputFormatter::Style::Html:
            type = QStringLiteral("text/html");
            break;
    }
    result.mimeType = isUtf8 ? type : QStringLiteral("%1; charset=%2").arg(type, charset);
    return result;
}

QString PDFDocumentTextExporter::getSystemCharset()
{
#ifdef Q_OS_WIN
    const UINT codePage = GetACP();
    if (codePage == CP_UTF8)
    {
        return QStringLiteral("UTF-8");
    }
    return QStringLiteral("windows-%1").arg(codePage);
#else
    return QStringLiteral("UTF-8");
#endif
}

QStringDecoder PDFDocumentTextExporter::createDecoder(const QString& mimeType)
{
    const qsizetype charsetIndex = mimeType.indexOf(QLatin1String("charset="), 0, Qt::CaseInsensitive);
    const QString charset = charsetIndex >= 0 ? mimeType.mid(charsetIndex + 8).section(QLatin1Char(';'), 0, 0).trimmed() : QStringLiteral("UTF-8");

    // Qt without ICU knows only UTF, Latin-1 and the encoding of the system by its own name
    // ("Locale"): the code page of Windows is decoded as the system encoding
    if (charset.compare(getSystemCharset(), Qt::CaseInsensitive) == 0 && charset.compare(QLatin1String("UTF-8"), Qt::CaseInsensitive) != 0)
    {
        return QStringDecoder(QStringConverter::System);
    }
    return QStringDecoder(charset.toLatin1().constData());
}

std::vector<PDFInteger> PDFDocumentTextExporter::findPagesWithText(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const PDFOperationControl* control, PDFProgressSink* progress)
{
    std::vector<PDFInteger> pagesWithText;
    if (!document || pageIndices.empty())
    {
        return pagesWithText;
    }

    if (progress)
    {
        progress->start(pageIndices.size(), ProgressStartupInfo());
    }

    // The caches of the layout algorithm (PDFDocumentTextFlowFactory::create)
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFCMSGeneric cms;
    PDFMeshQualitySettings meshQualitySettings;
    PDFOptionalContentActivity optionalContentActivity(document, OCUsage::Export, nullptr);
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    QMutex mutex;
    const PDFCatalog* catalog = document->getCatalog();
    auto scanPage = [&](PDFInteger pageIndex)
    {
        const PDFPage* page = catalog->getPage(pageIndex);
        if (page && !PDFOperationControl::isOperationCancelled(control))
        {
            PDFDocumentTextExporterPageControl pageControl(control);
            PDFDocumentTextExporterPageScanner scanner(PDFRenderer::IgnoreOptionalContent, page, document, &fontCache, &iccProfileCache, &cms, &optionalContentActivity, QTransform(), meshQualitySettings);
            scanner.setPageControl(&pageControl);
            scanner.processContents();

            if (pageControl.isTextFound())
            {
                QMutexLocker lock(&mutex);
                pagesWithText.push_back(pageIndex);
            }
        }

        if (progress)
        {
            progress->step();
        }
    };
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageIndices.cbegin(), pageIndices.cend(), scanPage);

    fontCache.setCacheShrinkEnabled(nullptr, true);
    if (progress)
    {
        progress->finish();
    }

    std::sort(pagesWithText.begin(), pagesWithText.end());
    return pagesWithText;
}

}   // namespace pdf
