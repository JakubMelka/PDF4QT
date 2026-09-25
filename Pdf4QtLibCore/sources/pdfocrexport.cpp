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

#include "pdfocrexport.h"
#include "pdfocrpagepreparer.h"
#include "pdfdocument.h"
#include "pdfcatalog.h"
#include "pdfconstants.h"

#include <QDir>
#include <QHash>
#include <QFileInfo>
#include <QSaveFile>
#include <QBuffer>
#include <QDateTime>
#include <QXmlStreamWriter>
#include <QRegularExpression>

#include <cmath>
#include <numbers>

namespace pdf
{

namespace
{

/// Word of the export with its geometry in the pixels of the export
struct ExportWord
{
    const PDFOCRWord* word = nullptr;
    QString text;
    QPolygonF polygon;
    QRect box;
};

/// Line of the export
struct ExportLine
{
    const PDFOCRLine* line = nullptr;
    std::vector<ExportWord> words;
    QRect box;

    /// Baseline in the pixels of the export (from the start to the end of the line)
    QLineF baseline;

    /// Angle of the text in degrees, counterclockwise (0 = horizontal text)
    int textAngle = 0;
};

/// Block of the export
struct ExportBlock
{
    const PDFOCRBlock* block = nullptr;
    std::vector<ExportLine> lines;
    QRect box;
};

/// Page of the export
struct ExportPage
{
    const PDFOCRPageResult* page = nullptr;
    std::vector<ExportBlock> blocks;
    QSize size;
    double dpi = 0.0;
    QString description;
};

QRect toPixelBox(const QRectF& rect, QSize size)
{
    const int left = qBound(0, int(std::floor(rect.left())), size.width());
    const int top = qBound(0, int(std::floor(rect.top())), size.height());
    const int right = qBound(0, int(std::ceil(rect.right())), size.width());
    const int bottom = qBound(0, int(std::ceil(rect.bottom())), size.height());
    return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
}

QRect unite(const QRect& first, const QRect& second)
{
    if (first.isNull())
    {
        return second;
    }
    if (second.isNull())
    {
        return first;
    }
    return first.united(second);
}

/// Returns the coordinates of the box as "left top right bottom" (exclusive right/bottom)
QString formatBox(const QRect& box)
{
    return QStringLiteral("%1 %2 %3 %4").arg(box.left()).arg(box.top()).arg(box.left() + box.width()).arg(box.top() + box.height());
}

QString getPageDescription(const PDFOCRPageResult& page)
{
    QString description = PDFTranslationContext::tr("Page %1").arg(page.pageIndex + 1);
    if (!page.pageLabel.isEmpty() && page.pageLabel != QString::number(page.pageIndex + 1))
    {
        description += QStringLiteral(" (%1)").arg(page.pageLabel);
    }
    return description;
}

bool isWordConfidenceLevel(const PDFOCRConfidence& confidence)
{
    return confidence.isAvailable() && (confidence.level == PDFOCRConfidenceLevel::Word || confidence.level == PDFOCRConfidenceLevel::Symbol);
}

/// Returns the confidence of the line, if the engine scores the lines only
std::optional<double> getLineConfidence(const PDFOCRLine& line)
{
    if (line.confidence.isAvailable() && line.confidence.level == PDFOCRConfidenceLevel::Line)
    {
        return line.confidence.normalized;
    }

    for (const PDFOCRWord& word : line.words)
    {
        if (word.confidence.isAvailable() && word.confidence.level == PDFOCRConfidenceLevel::Line)
        {
            return word.confidence.normalized;
        }
    }

    return std::nullopt;
}

/// Prepares the geometry of the page for the export
bool preparePage(const PDFDocument* document, const PDFOCRPageResult& page, const PDFOCRStructuredExporter::Options& options, ExportPage& exportPage)
{
    exportPage.page = &page;
    exportPage.dpi = PDFOCRStructuredExporter::getExportDpi(page, options);
    exportPage.description = getPageDescription(page);

    QTransform pageToExport;
    if (!PDFOCRStructuredExporter::getPageTransform(document, page.pageIndex, exportPage.dpi, &pageToExport, &exportPage.size))
    {
        return false;
    }

    for (const PDFOCRBlock& block : page.blocks)
    {
        ExportBlock exportBlock;
        exportBlock.block = &block;

        for (const PDFOCRLine& line : block.lines)
        {
            ExportLine exportLine;
            exportLine.line = &line;

            for (const PDFOCRWord& word : line.words)
            {
                if (word.reviewState == PDFOCRReviewState::Discarded ||
                    (options.onlyReviewed && word.reviewState == PDFOCRReviewState::Unreviewed) ||
                    word.text.trimmed().isEmpty() ||
                    !word.quad.isValid())
                {
                    continue;
                }

                ExportWord exportWord;
                exportWord.word = &word;
                exportWord.text = options.normalizeNFC ? word.text.normalized(QString::NormalizationForm_C) : word.text;
                exportWord.polygon = pageToExport.map(word.quad.toPolygon());
                exportWord.box = toPixelBox(exportWord.polygon.boundingRect(), exportPage.size);
                exportLine.box = unite(exportLine.box, exportWord.box);
                exportLine.words.push_back(std::move(exportWord));
            }

            if (exportLine.words.empty())
            {
                continue;
            }

            // Baseline and the angle of the text. The baseline goes from the start to
            // the end of the line, i.e. from the right to the left for right-to-left text.
            QLineF baseline = line.baseline;
            if (baseline.isNull() || baseline.length() <= 0.0)
            {
                const PDFOCRQuad& quad = line.quad.isValid() ? line.quad : exportLine.words.front().word->quad;
                baseline = QLineF(quad.points[0], quad.points[1]);
            }
            exportLine.baseline = QLineF(pageToExport.map(baseline.p1()), pageToExport.map(baseline.p2()));

            QPointF direction = exportLine.baseline.p2() - exportLine.baseline.p1();
            if (line.direction == PDFOCRTextDirection::RightToLeft)
            {
                direction = -direction;
            }
            if (!qFuzzyIsNull(direction.x()) || !qFuzzyIsNull(direction.y()))
            {
                // The y axis of the export grows downwards, the angle is counterclockwise
                exportLine.textAngle = qRound(std::atan2(-direction.y(), direction.x()) * 180.0 / std::numbers::pi);
                if (exportLine.textAngle < 0)
                {
                    exportLine.textAngle += 360;
                }
                if (exportLine.textAngle == 360)
                {
                    exportLine.textAngle = 0;
                }
            }

            exportBlock.box = unite(exportBlock.box, exportLine.box);
            exportBlock.lines.push_back(std::move(exportLine));
        }

        if (!exportBlock.lines.empty())
        {
            exportPage.blocks.push_back(std::move(exportBlock));
        }
    }

    return true;
}

/// Collects the engines of the pages ("tesseract 5.5.2")
QStringList getEngines(const std::vector<ExportPage>& pages)
{
    QStringList engines;
    for (const ExportPage& page : pages)
    {
        const PDFOCRProvenance& provenance = page.page->provenance;
        if (provenance.engineId.isEmpty())
        {
            continue;
        }

        const QString engine = provenance.engineVersion.isEmpty() ? provenance.engineId : QStringLiteral("%1 %2").arg(provenance.engineId, provenance.engineVersion);
        if (!engines.contains(engine))
        {
            engines << engine;
        }
    }
    return engines;
}

/// Collects the language tags of the words of the pages
QStringList getLanguages(const std::vector<ExportPage>& pages)
{
    QStringList languages;
    for (const ExportPage& page : pages)
    {
        for (const ExportBlock& block : page.blocks)
        {
            for (const ExportLine& line : block.lines)
            {
                for (const ExportWord& word : line.words)
                {
                    const QString tag = PDFOCRStructuredExporter::toLanguageTag(word.word->language);
                    if (!tag.isEmpty() && !languages.contains(tag))
                    {
                        languages << tag;
                    }
                }
            }
        }
    }
    return languages;
}

QString getImageFileName(const PDFOCRStructuredExporter::Options& options, const PDFOCRPageResult& page)
{
    if (options.imageFileNameTemplate.isEmpty())
    {
        return QString();
    }
    return options.imageFileNameTemplate.contains(QStringLiteral("%1")) ? options.imageFileNameTemplate.arg(page.pageIndex + 1) : options.imageFileNameTemplate;
}

// -------------------------------------------------------------------------
// hOCR
// -------------------------------------------------------------------------

QByteArray writeHocr(const std::vector<ExportPage>& pages, const PDFOCRStructuredExporter::Options& options)
{
    const QStringList engines = getEngines(pages);
    const QStringList languages = getLanguages(pages);

    QString system = QString::fromLatin1(PDF_LIBRARY_NAME);
    if (!engines.isEmpty())
    {
        system += QStringLiteral(" (%1)").arg(engines.join(QStringLiteral(", ")));
    }

    QStringList capabilities = { QStringLiteral("ocr_page"), QStringLiteral("ocr_carea"), QStringLiteral("ocr_par"), QStringLiteral("ocr_line"), QStringLiteral("ocrx_word"), QStringLiteral("ocrp_lang"), QStringLiteral("ocrp_dir") };
    if (options.includeConfidence)
    {
        capabilities << QStringLiteral("ocrp_wconf");
    }

    QString text;
    text += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    text += QStringLiteral("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n");
    text += QStringLiteral("<html xmlns=\"http://www.w3.org/1999/xhtml\">\n");
    text += QStringLiteral(" <head>\n");
    text += QStringLiteral("  <title>%1</title>\n").arg(options.title.toHtmlEscaped());
    text += QStringLiteral("  <meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\"/>\n");
    text += QStringLiteral("  <meta name=\"ocr-system\" content=\"%1\"/>\n").arg(system.toHtmlEscaped());
    text += QStringLiteral("  <meta name=\"ocr-capabilities\" content=\"%1\"/>\n").arg(capabilities.join(QChar(' ')));
    text += QStringLiteral("  <meta name=\"ocr-number-of-pages\" content=\"%1\"/>\n").arg(pages.size());
    if (!languages.isEmpty())
    {
        text += QStringLiteral("  <meta name=\"ocr-langs\" content=\"%1\"/>\n").arg(languages.join(QChar(' ')).toHtmlEscaped());
    }
    text += QStringLiteral(" </head>\n");
    text += QStringLiteral(" <body>\n");

    for (const ExportPage& page : pages)
    {
        const int pageNumber = int(page.page->pageIndex) + 1;
        QStringList pageTitle;
        const QString imageFileName = getImageFileName(options, *page.page);
        if (!imageFileName.isEmpty())
        {
            pageTitle << QStringLiteral("image \"%1\"").arg(QString(imageFileName).replace(QChar('"'), QStringLiteral("\\\"")));
        }
        pageTitle << QStringLiteral("bbox 0 0 %1 %2").arg(page.size.width()).arg(page.size.height());
        pageTitle << QStringLiteral("ppageno %1").arg(page.page->pageIndex);
        pageTitle << QStringLiteral("scan_res %1 %1").arg(qRound(page.dpi));

        text += QStringLiteral("  <div class=\"ocr_page\" id=\"page_%1\" title=\"%2\">\n").arg(pageNumber).arg(pageTitle.join(QStringLiteral("; ")).toHtmlEscaped());

        int blockNumber = 0;
        int lineNumber = 0;
        int wordNumber = 0;
        for (const ExportBlock& block : page.blocks)
        {
            ++blockNumber;
            text += QStringLiteral("   <div class=\"ocr_carea\" id=\"block_%1_%2\" title=\"bbox %3\">\n").arg(pageNumber).arg(blockNumber).arg(formatBox(block.box));
            text += QStringLiteral("    <p class=\"ocr_par\" id=\"par_%1_%2\" title=\"bbox %3\">\n").arg(pageNumber).arg(blockNumber).arg(formatBox(block.box));

            for (const ExportLine& line : block.lines)
            {
                ++lineNumber;
                QStringList lineTitle;
                lineTitle << QStringLiteral("bbox %1").arg(formatBox(line.box));

                // Baseline: slope and offset relative to the bottom-left corner of the line box
                const QPointF delta = line.baseline.p2() - line.baseline.p1();
                if (std::abs(delta.x()) > 1e-3 && std::abs(delta.y()) < std::abs(delta.x()))
                {
                    const double slope = delta.y() / delta.x();
                    const double yAtLeft = line.baseline.p1().y() + slope * (line.box.left() - line.baseline.p1().x());
                    const double offset = yAtLeft - (line.box.top() + line.box.height());
                    lineTitle << QStringLiteral("baseline %1 %2").arg(QString::number(slope, 'f', 3)).arg(qRound(offset));
                }
                if (line.textAngle != 0)
                {
                    lineTitle << QStringLiteral("textangle %1").arg(line.textAngle);
                }

                const std::optional<double> lineConfidence = getLineConfidence(*line.line);
                if (options.includeConfidence && lineConfidence)
                {
                    lineTitle << QStringLiteral("x_wconf %1").arg(qRound(*lineConfidence));
                }

                const bool rightToLeft = line.line->direction == PDFOCRTextDirection::RightToLeft;
                text += QStringLiteral("     <span class=\"ocr_line\" id=\"line_%1_%2\"%3 title=\"%4\">").arg(pageNumber).arg(lineNumber)
                                                                                                        .arg(rightToLeft ? QStringLiteral(" dir=\"rtl\"") : QString())
                                                                                                        .arg(lineTitle.join(QStringLiteral("; ")));

                bool first = true;
                for (const ExportWord& word : line.words)
                {
                    ++wordNumber;
                    QStringList wordTitle;
                    wordTitle << QStringLiteral("bbox %1").arg(formatBox(word.box));
                    if (options.includeConfidence && isWordConfidenceLevel(word.word->confidence))
                    {
                        wordTitle << QStringLiteral("x_wconf %1").arg(qRound(word.word->confidence.normalized.value()));
                    }

                    const QString language = PDFOCRStructuredExporter::toLanguageTag(word.word->language);
                    if (!first)
                    {
                        text += QChar(' ');
                    }
                    first = false;

                    text += QStringLiteral("<span class=\"ocrx_word\" id=\"word_%1_%2\"%3 title=\"%4\">%5</span>").arg(pageNumber).arg(wordNumber)
                                                                                                          .arg(language.isEmpty() ? QString() : QStringLiteral(" lang=\"%1\"").arg(language))
                                                                                                          .arg(wordTitle.join(QStringLiteral("; ")), word.text.toHtmlEscaped());
                }

                text += QStringLiteral("</span>\n");
            }

            text += QStringLiteral("    </p>\n");
            text += QStringLiteral("   </div>\n");
        }

        text += QStringLiteral("  </div>\n");
    }

    text += QStringLiteral(" </body>\n");
    text += QStringLiteral("</html>\n");
    return text.toUtf8();
}

// -------------------------------------------------------------------------
// ALTO
// -------------------------------------------------------------------------

QString formatPoints(const QPolygonF& polygon)
{
    QStringList points;
    for (const QPointF& point : polygon)
    {
        points << QStringLiteral("%1,%2").arg(qRound(point.x())).arg(qRound(point.y()));
    }
    return points.join(QChar(' '));
}

/// Returns true, if the word is the first part of a word hyphenated at the end of
/// the line and the next line continues it (the same rule as the text export:
/// only a lowercase continuation is a continuation of the word)
bool isHyphenationPart1(const ExportLine& line, size_t wordIndex, const ExportLine* nextLine)
{
    if (!nextLine || wordIndex + 1 != line.words.size() || nextLine->words.empty())
    {
        return false;
    }

    const QString& text = line.words[wordIndex].text;
    const QString& next = nextLine->words.front().text;
    return text.size() > 1 && text.endsWith(QChar('-')) && text.at(text.size() - 2).isLetter() && !next.isEmpty() && next.front().isLower();
}

QByteArray writeAlto(const std::vector<ExportPage>& pages, const PDFOCRStructuredExporter::Options& options)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QBuffer::WriteOnly);

    QXmlStreamWriter writer(&buffer);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(2);
    writer.writeStartDocument();

    const QString altoNamespace = QStringLiteral("http://www.loc.gov/standards/alto/ns-v4#");
    writer.writeDefaultNamespace(altoNamespace);
    writer.writeNamespace(QStringLiteral("http://www.w3.org/2001/XMLSchema-instance"), QStringLiteral("xsi"));
    writer.writeStartElement(altoNamespace, QStringLiteral("alto"));
    writer.writeAttribute(QStringLiteral("http://www.w3.org/2001/XMLSchema-instance"), QStringLiteral("schemaLocation"),
                          QStringLiteral("http://www.loc.gov/standards/alto/ns-v4# http://www.loc.gov/alto/v4/alto-4-4.xsd"));

    // Description
    writer.writeStartElement(QStringLiteral("Description"));
    writer.writeTextElement(QStringLiteral("MeasurementUnit"), QStringLiteral("pixel"));
    if (pages.size() == 1)
    {
        const QString imageFileName = getImageFileName(options, *pages.front().page);
        if (!imageFileName.isEmpty())
        {
            writer.writeStartElement(QStringLiteral("sourceImageInformation"));
            writer.writeTextElement(QStringLiteral("fileName"), imageFileName);
            writer.writeEndElement();
        }
    }

    const QStringList engines = getEngines(pages);
    writer.writeStartElement(QStringLiteral("Processing"));
    writer.writeAttribute(QStringLiteral("ID"), QStringLiteral("OCR_0"));
    writer.writeTextElement(QStringLiteral("processingCategory"), QStringLiteral("contentGeneration"));
    writer.writeTextElement(QStringLiteral("processingDateTime"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!engines.isEmpty())
    {
        writer.writeTextElement(QStringLiteral("processingStepSettings"), QStringLiteral("engine: %1").arg(engines.join(QStringLiteral(", "))));
    }
    writer.writeStartElement(QStringLiteral("processingSoftware"));
    writer.writeTextElement(QStringLiteral("softwareName"), QStringLiteral("PDF4QT"));
    writer.writeTextElement(QStringLiteral("softwareVersion"), QString::fromLatin1(PDF_LIBRARY_VERSION));
    writer.writeEndElement(); // processingSoftware
    writer.writeEndElement(); // Processing
    writer.writeEndElement(); // Description

    writer.writeStartElement(QStringLiteral("Layout"));
    for (const ExportPage& page : pages)
    {
        const int pageNumber = int(page.page->pageIndex) + 1;
        writer.writeStartElement(QStringLiteral("Page"));
        writer.writeAttribute(QStringLiteral("ID"), QStringLiteral("page_%1").arg(pageNumber));
        writer.writeAttribute(QStringLiteral("PHYSICAL_IMG_NR"), QString::number(pageNumber));
        if (!page.page->pageLabel.isEmpty())
        {
            writer.writeAttribute(QStringLiteral("PRINTED_IMG_NR"), page.page->pageLabel);
        }
        writer.writeAttribute(QStringLiteral("WIDTH"), QString::number(page.size.width()));
        writer.writeAttribute(QStringLiteral("HEIGHT"), QString::number(page.size.height()));

        writer.writeStartElement(QStringLiteral("PrintSpace"));
        writer.writeAttribute(QStringLiteral("HPOS"), QStringLiteral("0"));
        writer.writeAttribute(QStringLiteral("VPOS"), QStringLiteral("0"));
        writer.writeAttribute(QStringLiteral("WIDTH"), QString::number(page.size.width()));
        writer.writeAttribute(QStringLiteral("HEIGHT"), QString::number(page.size.height()));

        auto writeBox = [&writer](const QRect& box)
        {
            writer.writeAttribute(QStringLiteral("HPOS"), QString::number(box.left()));
            writer.writeAttribute(QStringLiteral("VPOS"), QString::number(box.top()));
            writer.writeAttribute(QStringLiteral("WIDTH"), QString::number(box.width()));
            writer.writeAttribute(QStringLiteral("HEIGHT"), QString::number(box.height()));
        };

        int blockNumber = 0;
        int lineNumber = 0;
        int wordNumber = 0;
        for (const ExportBlock& block : page.blocks)
        {
            ++blockNumber;
            writer.writeStartElement(QStringLiteral("TextBlock"));
            writer.writeAttribute(QStringLiteral("ID"), QStringLiteral("block_%1_%2").arg(pageNumber).arg(blockNumber));
            writeBox(block.box);

            for (size_t lineIndex = 0; lineIndex < block.lines.size(); ++lineIndex)
            {
                const ExportLine& line = block.lines[lineIndex];
                const ExportLine* nextLine = lineIndex + 1 < block.lines.size() ? &block.lines[lineIndex + 1] : nullptr;
                const ExportLine* previousLine = lineIndex > 0 ? &block.lines[lineIndex - 1] : nullptr;

                ++lineNumber;
                writer.writeStartElement(QStringLiteral("TextLine"));
                writer.writeAttribute(QStringLiteral("ID"), QStringLiteral("line_%1_%2").arg(pageNumber).arg(lineNumber));
                writeBox(line.box);
                writer.writeAttribute(QStringLiteral("BASELINE"), formatPoints(QPolygonF() << line.baseline.p1() << line.baseline.p2()));

                for (size_t wordIndex = 0; wordIndex < line.words.size(); ++wordIndex)
                {
                    const ExportWord& word = line.words[wordIndex];
                    ++wordNumber;

                    if (wordIndex > 0)
                    {
                        // Space between the words (its geometry, if the words do not overlap)
                        const QRect& previousBox = line.words[wordIndex - 1].box;
                        writer.writeStartElement(QStringLiteral("SP"));
                        const int left = qMin(previousBox.right(), word.box.right()) + 1;
                        const int right = qMax(previousBox.left(), word.box.left());
                        if (right > left)
                        {
                            writer.writeAttribute(QStringLiteral("WIDTH"), QString::number(right - left));
                            writer.writeAttribute(QStringLiteral("HPOS"), QString::number(left));
                            writer.writeAttribute(QStringLiteral("VPOS"), QString::number(line.box.top()));
                        }
                        writer.writeEndElement();
                    }

                    const bool part1 = isHyphenationPart1(line, wordIndex, nextLine);
                    const bool part2 = wordIndex == 0 && previousLine && isHyphenationPart1(*previousLine, previousLine->words.size() - 1, &line);

                    writer.writeStartElement(QStringLiteral("String"));
                    writer.writeAttribute(QStringLiteral("ID"), QStringLiteral("string_%1_%2").arg(pageNumber).arg(wordNumber));
                    writeBox(word.box);
                    writer.writeAttribute(QStringLiteral("CONTENT"), part1 ? word.text.chopped(1) : word.text);

                    if (part1 || part2)
                    {
                        const QString& firstPart = part1 ? word.text : previousLine->words.back().text;
                        const QString& secondPart = part1 ? nextLine->words.front().text : word.text;
                        writer.writeAttribute(QStringLiteral("SUBS_TYPE"), part1 ? QStringLiteral("HypPart1") : QStringLiteral("HypPart2"));
                        writer.writeAttribute(QStringLiteral("SUBS_CONTENT"), firstPart.chopped(1) + secondPart);
                    }

                    if (options.includeConfidence && isWordConfidenceLevel(word.word->confidence))
                    {
                        writer.writeAttribute(QStringLiteral("WC"), QString::number(qBound(0.0, word.word->confidence.normalized.value() / 100.0, 1.0), 'f', 3));
                    }

                    const QString language = PDFOCRStructuredExporter::toLanguageTag(word.word->language);
                    if (!language.isEmpty())
                    {
                        writer.writeAttribute(QStringLiteral("LANG"), language);
                    }

                    // The exact geometry of a rotated word
                    const QPointF bottom = word.polygon.size() >= 2 ? word.polygon[1] - word.polygon[0] : QPointF();
                    const double angle = std::abs(std::atan2(bottom.y(), bottom.x()) * 180.0 / std::numbers::pi);
                    if (word.polygon.size() >= 4 && angle >= 1.0 && angle <= 179.0)
                    {
                        writer.writeStartElement(QStringLiteral("Shape"));
                        writer.writeStartElement(QStringLiteral("Polygon"));
                        writer.writeAttribute(QStringLiteral("POINTS"), formatPoints(word.polygon));
                        writer.writeEndElement();
                        writer.writeEndElement();
                    }

                    writer.writeEndElement(); // String

                    if (part1)
                    {
                        writer.writeStartElement(QStringLiteral("HYP"));
                        writer.writeAttribute(QStringLiteral("CONTENT"), QStringLiteral("-"));
                        writer.writeEndElement();
                    }
                }

                writer.writeEndElement(); // TextLine
            }

            writer.writeEndElement(); // TextBlock
        }

        writer.writeEndElement(); // PrintSpace
        writer.writeEndElement(); // Page
    }
    writer.writeEndElement(); // Layout

    writer.writeEndElement(); // alto
    writer.writeEndDocument();
    buffer.close();
    return data;
}

// -------------------------------------------------------------------------
// TSV
// -------------------------------------------------------------------------

QByteArray writeTsv(const std::vector<ExportPage>& pages, const PDFOCRStructuredExporter::Options& options)
{
    QString text = QStringLiteral("level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n");

    auto addRow = [&text](int level, int page, int block, int paragraph, int line, int word, const QRect& box, const QString& confidence, QString content)
    {
        content.replace(QChar('\t'), QChar(' '));
        content.replace(QChar('\n'), QChar(' '));
        content.replace(QChar('\r'), QChar(' '));
        text += QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\t%9\t%10\t%11\t%12\n").arg(level).arg(page).arg(block).arg(paragraph).arg(line).arg(word)
                                                                                     .arg(box.left()).arg(box.top()).arg(box.width()).arg(box.height()).arg(confidence, content);
    };

    const QString unknown = QStringLiteral("-1");
    for (const ExportPage& page : pages)
    {
        const int pageNumber = int(page.page->pageIndex) + 1;
        addRow(1, pageNumber, 0, 0, 0, 0, QRect(QPoint(0, 0), page.size), unknown, QString());

        int blockNumber = 0;
        for (const ExportBlock& block : page.blocks)
        {
            ++blockNumber;
            addRow(2, pageNumber, blockNumber, 0, 0, 0, block.box, unknown, QString());
            addRow(3, pageNumber, blockNumber, 1, 0, 0, block.box, unknown, QString());

            int lineNumber = 0;
            for (const ExportLine& line : block.lines)
            {
                ++lineNumber;
                const std::optional<double> lineConfidence = getLineConfidence(*line.line);
                addRow(4, pageNumber, blockNumber, 1, lineNumber, 0, line.box, (options.includeConfidence && lineConfidence) ? QString::number(*lineConfidence, 'f', 6) : unknown, QString());

                int wordNumber = 0;
                for (const ExportWord& word : line.words)
                {
                    ++wordNumber;
                    const bool hasConfidence = options.includeConfidence && isWordConfidenceLevel(word.word->confidence);
                    addRow(5, pageNumber, blockNumber, 1, lineNumber, wordNumber, word.box, hasConfidence ? QString::number(word.word->confidence.normalized.value(), 'f', 6) : unknown, word.text);
                }
            }
        }
    }

    return text.toUtf8();
}

} // namespace

// -------------------------------------------------------------------------
// PDFOCRStructuredExporter
// -------------------------------------------------------------------------

QByteArray PDFOCRStructuredExporter::exportPages(const PDFDocument* document,
                                                 const std::vector<const PDFOCRPageResult*>& pages,
                                                 const Options& options,
                                                 PDFOCRTextExporter::Report* report)
{
    std::vector<ExportPage> exportPages;
    for (const PDFOCRPageResult* page : pages)
    {
        if (!page)
        {
            continue;
        }

        const QString description = getPageDescription(*page);
        if (!page->hasResult())
        {
            if (report)
            {
                report->skippedPages.push_back(page->pageIndex);
                report->skippedDescriptions << QStringLiteral("%1: %2").arg(description, PDFOCRTextExporter::getPageStateDescription(*page));
            }
            continue;
        }

        ExportPage exportPage;
        if (!preparePage(document, *page, options, exportPage))
        {
            if (report)
            {
                report->skippedPages.push_back(page->pageIndex);
                report->skippedDescriptions << QStringLiteral("%1: %2").arg(description, PDFTranslationContext::tr("the page is not a part of the document"));
            }
            continue;
        }

        if (report)
        {
            if (page->state == PDFOCRPageState::NoText)
            {
                report->noTextDescriptions << QStringLiteral("%1: %2").arg(description, PDFOCRTextExporter::getPageStateDescription(*page));
            }

            report->exportedPages.push_back(page->pageIndex);
            report->pageDescriptions << description;
            for (const ExportBlock& block : exportPage.blocks)
            {
                for (const ExportLine& line : block.lines)
                {
                    report->wordCount += int(line.words.size());
                }
            }
        }

        exportPages.push_back(std::move(exportPage));
    }

    switch (options.format)
    {
        case Format::Hocr:
            return writeHocr(exportPages, options);
        case Format::Alto:
            return writeAlto(exportPages, options);
        case Format::Tsv:
            return writeTsv(exportPages, options);
    }

    return QByteArray();
}

double PDFOCRStructuredExporter::getExportDpi(const PDFOCRPageResult& page, const Options& options)
{
    if (options.dpi > 0.0 && std::isfinite(options.dpi))
    {
        return options.dpi;
    }

    if (page.geometry.dpi > 0.0 && std::isfinite(page.geometry.dpi))
    {
        return page.geometry.dpi;
    }

    return DefaultDpi;
}

bool PDFOCRStructuredExporter::getPageTransform(const PDFDocument* document, PDFInteger pageIndex, double dpi, QTransform* pageToExport, QSize* size)
{
    if (!document || pageIndex < 0 || pageIndex >= PDFInteger(document->getCatalog()->getPageCount()))
    {
        return false;
    }

    const PDFPage* page = document->getCatalog()->getPage(pageIndex);
    const QSize rasterSize = page ? PDFOCRPagePreparer::getRasterSize(page, dpi) : QSize();
    if (rasterSize.isEmpty())
    {
        return false;
    }

    *pageToExport = PDFOCRPagePreparer::getPageToRasterMatrix(page, rasterSize);
    *size = rasterSize;
    return true;
}

QString PDFOCRStructuredExporter::toLanguageTag(const QString& engineLanguage)
{
    // Models of scripts ("script/Latin") are not languages
    const QString language = engineLanguage.section(QChar('@'), 0, 0).trimmed();
    if (language.isEmpty() || language.contains(QChar('/')))
    {
        return QString();
    }

    static const QHash<QString, QString> tags =
    {
        { QStringLiteral("afr"), QStringLiteral("af") },
        { QStringLiteral("ara"), QStringLiteral("ar") },
        { QStringLiteral("bel"), QStringLiteral("be") },
        { QStringLiteral("ben"), QStringLiteral("bn") },
        { QStringLiteral("bul"), QStringLiteral("bg") },
        { QStringLiteral("cat"), QStringLiteral("ca") },
        { QStringLiteral("ces"), QStringLiteral("cs") },
        { QStringLiteral("chi_sim"), QStringLiteral("zh-Hans") },
        { QStringLiteral("chi_sim_vert"), QStringLiteral("zh-Hans") },
        { QStringLiteral("chi_tra"), QStringLiteral("zh-Hant") },
        { QStringLiteral("chi_tra_vert"), QStringLiteral("zh-Hant") },
        { QStringLiteral("cym"), QStringLiteral("cy") },
        { QStringLiteral("dan"), QStringLiteral("da") },
        { QStringLiteral("deu"), QStringLiteral("de") },
        { QStringLiteral("deu_latf"), QStringLiteral("de-Latf") },
        { QStringLiteral("ell"), QStringLiteral("el") },
        { QStringLiteral("eng"), QStringLiteral("en") },
        { QStringLiteral("est"), QStringLiteral("et") },
        { QStringLiteral("eus"), QStringLiteral("eu") },
        { QStringLiteral("fas"), QStringLiteral("fa") },
        { QStringLiteral("fin"), QStringLiteral("fi") },
        { QStringLiteral("fra"), QStringLiteral("fr") },
        { QStringLiteral("gle"), QStringLiteral("ga") },
        { QStringLiteral("glg"), QStringLiteral("gl") },
        { QStringLiteral("heb"), QStringLiteral("he") },
        { QStringLiteral("hin"), QStringLiteral("hi") },
        { QStringLiteral("hrv"), QStringLiteral("hr") },
        { QStringLiteral("hun"), QStringLiteral("hu") },
        { QStringLiteral("hye"), QStringLiteral("hy") },
        { QStringLiteral("ind"), QStringLiteral("id") },
        { QStringLiteral("isl"), QStringLiteral("is") },
        { QStringLiteral("ita"), QStringLiteral("it") },
        { QStringLiteral("jpn"), QStringLiteral("ja") },
        { QStringLiteral("jpn_vert"), QStringLiteral("ja") },
        { QStringLiteral("kat"), QStringLiteral("ka") },
        { QStringLiteral("kaz"), QStringLiteral("kk") },
        { QStringLiteral("kor"), QStringLiteral("ko") },
        { QStringLiteral("kor_vert"), QStringLiteral("ko") },
        { QStringLiteral("lat"), QStringLiteral("la") },
        { QStringLiteral("lav"), QStringLiteral("lv") },
        { QStringLiteral("lit"), QStringLiteral("lt") },
        { QStringLiteral("mkd"), QStringLiteral("mk") },
        { QStringLiteral("mlt"), QStringLiteral("mt") },
        { QStringLiteral("msa"), QStringLiteral("ms") },
        { QStringLiteral("nld"), QStringLiteral("nl") },
        { QStringLiteral("nor"), QStringLiteral("no") },
        { QStringLiteral("pol"), QStringLiteral("pl") },
        { QStringLiteral("por"), QStringLiteral("pt") },
        { QStringLiteral("ron"), QStringLiteral("ro") },
        { QStringLiteral("rus"), QStringLiteral("ru") },
        { QStringLiteral("slk"), QStringLiteral("sk") },
        { QStringLiteral("slv"), QStringLiteral("sl") },
        { QStringLiteral("spa"), QStringLiteral("es") },
        { QStringLiteral("sqi"), QStringLiteral("sq") },
        { QStringLiteral("srp"), QStringLiteral("sr-Cyrl") },
        { QStringLiteral("srp_latn"), QStringLiteral("sr-Latn") },
        { QStringLiteral("swe"), QStringLiteral("sv") },
        { QStringLiteral("tha"), QStringLiteral("th") },
        { QStringLiteral("tur"), QStringLiteral("tr") },
        { QStringLiteral("ukr"), QStringLiteral("uk") },
        { QStringLiteral("vie"), QStringLiteral("vi") },
        { QStringLiteral("yid"), QStringLiteral("yi") },
    };

    auto it = tags.constFind(language);
    if (it != tags.cend())
    {
        return it.value();
    }

    // The other codes are ISO 639-2/3 codes, which are valid language tags,
    // unless they carry a variant with an underscore
    static const QRegularExpression validTag(QStringLiteral("^[a-z]{2,3}$"));
    return validTag.match(language).hasMatch() ? language : QString();
}

QString PDFOCRStructuredExporter::getFormatName(Format format)
{
    switch (format)
    {
        case Format::Hocr:
            return PDFTranslationContext::tr("hOCR (HTML)");
        case Format::Alto:
            return PDFTranslationContext::tr("ALTO XML 4");
        case Format::Tsv:
            return PDFTranslationContext::tr("TSV (Tesseract compatible)");
    }
    return QString();
}

QString PDFOCRStructuredExporter::getFileSuffix(Format format)
{
    switch (format)
    {
        case Format::Hocr:
            return QStringLiteral("hocr");
        case Format::Alto:
            return QStringLiteral("xml");
        case Format::Tsv:
            return QStringLiteral("tsv");
    }
    return QString();
}

QString PDFOCRStructuredExporter::getPageFileName(const QString& fileName, PDFInteger pageIndex, PDFInteger pageCount)
{
    const QFileInfo fileInfo(fileName);
    const int digits = qMax(3, int(QString::number(qMax<PDFInteger>(pageCount, 1)).size()));
    const QString suffix = fileInfo.suffix();
    const QString baseName = suffix.isEmpty() ? fileInfo.fileName() : fileInfo.completeBaseName();
    const QString pageFileName = QStringLiteral("%1_p%2").arg(baseName).arg(pageIndex + 1, digits, 10, QChar('0')) + (suffix.isEmpty() ? QString() : QChar('.') + suffix);
    return fileInfo.dir().filePath(pageFileName);
}

bool PDFOCRStructuredExporter::writeFile(const QString& fileName, const QByteArray& data, QString* errorMessage)
{
    QSaveFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Cannot open file '%1' for writing: %2").arg(fileName, file.errorString());
        }
        return false;
    }

    if (file.write(data) != data.size() || !file.commit())
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Cannot write file '%1': %2").arg(fileName, file.errorString());
        }
        return false;
    }

    return true;
}

QString PDFOCRStructuredExporter::getFileFilter(Format format)
{
    switch (format)
    {
        case Format::Hocr:
            return PDFTranslationContext::tr("hOCR (*.hocr *.html)");
        case Format::Alto:
            return PDFTranslationContext::tr("ALTO XML (*.xml)");
        case Format::Tsv:
            return PDFTranslationContext::tr("Tab separated values (*.tsv)");
    }
    return QString();
}

}   // namespace pdf
