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

#include "pdfappannotationinfo.h"
#include "pdfapplocale.h"
#include "pdfappdocumentpanels_notes.h"

#include "pdfannotationmanipulator.h"

namespace pdf
{

/// Texts of the annotation info
class PDFAppAnnotationInfoHelper
{
public:
    PDFAppAnnotationInfoHelper() = delete;

    /// Bidirectional controls (embeddings, overrides, isolates, marks): a name of
    /// the document must not reorder the text around it (owner, security review)
    static bool isBidiControl(QChar character)
    {
        const char16_t code = character.unicode();
        return (code >= 0x202A && code <= 0x202E) || (code >= 0x2066 && code <= 0x2069) || code == 0x200E || code == 0x200F || code == 0x061C;
    }

    /// A text of the document shown in a single line: bounded, control characters
    /// replaced by spaces, bidirectional controls removed
    static QString sanitizeLine(const QString& text, qsizetype length)
    {
        QString result;
        const QString bounded = text.left(length);
        result.reserve(bounded.size());
        for (const QChar character : bounded)
        {
            if (isBidiControl(character))
            {
                continue;
            }
            if (character.category() == QChar::Other_Control || character == QChar::LineSeparator || character == QChar::ParagraphSeparator)
            {
                result.append(QChar(u' '));
                continue;
            }
            result.append(character);
        }
        return result.simplified();
    }

    /// A text of the document shown in several lines: bounded, control characters
    /// other than the line breaks removed
    static QString sanitizeText(const QString& text, qsizetype length)
    {
        QString result;
        const QString bounded = text.left(length);
        result.reserve(bounded.size());
        for (const QChar character : bounded)
        {
            if (character == QChar(u'\r'))
            {
                continue;
            }
            if (character == QChar(u'\n') || character.category() != QChar::Other_Control)
            {
                result.append(character);
            }
        }
        return result;
    }

    /// Readable name of a stamp of the standard names (else the name itself)
    static QString getStampName(const QByteArray& name)
    {
        static const std::pair<const char*, const char*> stamps[] = {
            { "Approved", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "Approved") },
            { "AsIs", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "As is") },
            { "Confidential", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "Confidential") },
            { "Departmental", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "Departmental") },
            { "Draft", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "Draft") },
            { "Experimental", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "Experimental") },
            { "Expired", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "Expired") },
            { "Final", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "Final") },
            { "ForComment", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "For comment") },
            { "ForPublicRelease", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "For public release") },
            { "NotApproved", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "Not approved") },
            { "NotForPublicRelease", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "Not for public release") },
            { "Sold", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "Sold") },
            { "TopSecret", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationInfo", "Top secret") }
        };

        for (const auto& item : stamps)
        {
            if (name == item.first)
            {
                return PDFAppAnnotationInfo::tr(item.second);
            }
        }
        return sanitizeLine(QString::fromLatin1(name), 64);
    }
};

PDFAppAnnotationInfo PDFAppAnnotationInfo::read(const PDFDocument* document, PDFObjectReference annotation)
{
    using H = PDFAppAnnotationInfoHelper;

    PDFAppAnnotationInfo info;
    if (!document || !annotation.isValid() || !document->getObjectByReference(annotation).isDictionary())
    {
        return info;
    }

    const PDFObjectStorage* storage = &document->getStorage();
    const PDFAnnotationPtr parsed = PDFAnnotation::parse(storage, annotation);
    const PDFObjectReference page = parsed ? PDFAnnotationManipulator::findAnnotationPage(storage, annotation) : PDFObjectReference();
    if (!parsed || !page.isValid())
    {
        return info;
    }

    const size_t pageIndex = document->getCatalog()->getPageIndexFromPageReference(page);
    info.reference = annotation;
    info.pageReference = page;
    info.pageIndex = pageIndex < document->getCatalog()->getPageCount() ? PDFInteger(pageIndex) : PDFInteger(-1);
    info.type = parsed->getType();
    info.contents = H::sanitizeText(parsed->getContents(), MAXIMUM_CONTENTS_LENGTH);
    info.modified = parsed->getLastModifiedDateTime();
    info.rectangle = parsed->getRectangle().normalized();
    info.flags = parsed->getFlags();

    if (const PDFMarkupAnnotation* markup = parsed->asMarkupAnnotation())
    {
        info.isMarkup = true;
        info.isReply = markup->getInReplyTo().isValid();
        info.author = H::sanitizeLine(markup->getWindowTitle(), MAXIMUM_NAME_LENGTH);
        info.subject = H::sanitizeLine(markup->getSubject(), MAXIMUM_NAME_LENGTH);
        info.created = markup->getCreationDate();
    }

    PDFDocumentDataLoaderDecorator loader(document);
    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(annotation)))
    {
        info.iconName = loader.readNameFromDictionary(dictionary, "Name").left(127);
    }
    return info;
}

std::vector<PDFAppAnnotationInfo> PDFAppAnnotationInfo::readReplies(const PDFDocument* document, PDFObjectReference annotation)
{
    std::vector<PDFAppAnnotationInfo> replies;
    if (!document || !annotation.isValid())
    {
        return replies;
    }

    const PDFObjectStorage* storage = &document->getStorage();
    const PDFObjectReference page = PDFAnnotationManipulator::findAnnotationPage(storage, annotation);
    for (const PDFObjectReference& reply : PDFAnnotationManipulator::getReplies(storage, page, annotation))
    {
        if (replies.size() >= MAXIMUM_REPLIES)
        {
            break;
        }

        PDFAppAnnotationInfo info = read(document, reply);
        if (info.isValid())
        {
            replies.push_back(std::move(info));
        }
    }
    return replies;
}

QString PDFAppAnnotationInfo::getTypeName(AnnotationType type)
{
    switch (type)
    {
        case AnnotationType::Text:
            return tr("Sticky note");
        case AnnotationType::Link:
            return tr("Link");
        case AnnotationType::FreeText:
            return tr("Text box");
        case AnnotationType::Line:
            return tr("Line");
        case AnnotationType::Square:
            return tr("Rectangle");
        case AnnotationType::Circle:
            return tr("Ellipse");
        case AnnotationType::Polygon:
            return tr("Polygon");
        case AnnotationType::Polyline:
            return tr("Polyline");
        case AnnotationType::Highlight:
            return tr("Highlight");
        case AnnotationType::Underline:
            return tr("Underline");
        case AnnotationType::Squiggly:
            return tr("Squiggly underline");
        case AnnotationType::StrikeOut:
            return tr("Strikethrough");
        case AnnotationType::Stamp:
            return tr("Stamp");
        case AnnotationType::Caret:
            return tr("Caret");
        case AnnotationType::Ink:
            return tr("Freehand");
        case AnnotationType::FileAttachment:
            return tr("File attachment");
        case AnnotationType::Sound:
            return tr("Sound");
        case AnnotationType::Redact:
            return tr("Redaction");
        case AnnotationType::Widget:
            return tr("Form field");
        default:
            break;
    }
    return tr("Annotation");
}

QString PDFAppAnnotationInfo::getPropertiesTitle(AnnotationType type)
{
    switch (type)
    {
        case AnnotationType::Text:
            return tr("Properties of the sticky note");
        case AnnotationType::Link:
            return tr("Properties of the link");
        case AnnotationType::FreeText:
            return tr("Properties of the text box");
        case AnnotationType::Line:
            return tr("Properties of the line");
        case AnnotationType::Square:
            return tr("Properties of the rectangle");
        case AnnotationType::Circle:
            return tr("Properties of the ellipse");
        case AnnotationType::Polygon:
            return tr("Properties of the polygon");
        case AnnotationType::Polyline:
            return tr("Properties of the polyline");
        case AnnotationType::Highlight:
            return tr("Properties of the highlight");
        case AnnotationType::Underline:
            return tr("Properties of the underline");
        case AnnotationType::Squiggly:
            return tr("Properties of the squiggly underline");
        case AnnotationType::StrikeOut:
            return tr("Properties of the strikethrough");
        case AnnotationType::Stamp:
            return tr("Properties of the stamp");
        case AnnotationType::Caret:
            return tr("Properties of the caret");
        case AnnotationType::Ink:
            return tr("Properties of the freehand drawing");
        case AnnotationType::FileAttachment:
            return tr("Properties of the file attachment");
        default:
            break;
    }
    return tr("Properties of the annotation");
}

QString PDFAppAnnotationInfo::getDisplayName(const PDFAppAnnotationInfo& info)
{
    if (info.type == AnnotationType::Stamp && !info.iconName.isEmpty())
    {
        return tr("Stamp “%1”").arg(PDFAppAnnotationInfoHelper::getStampName(info.iconName));
    }
    return getTypeName(info.type);
}

void PDFAppAnnotationInfo::getBadge(AnnotationType type, bool isReply, QString* iconName, PDFAccentHue* hue)
{
    if (type == AnnotationType::Link)
    {
        *iconName = QStringLiteral("link");
        *hue = PDFAccentHue::Blue;
        return;
    }
    if (!PDFAppNotes::isListedType(type) && !isReply)
    {
        *iconName = QStringLiteral("square");
        *hue = PDFAccentHue::Neutral;
        return;
    }
    PDFAppNotes::getBadge(type, isReply, iconName, hue);
}

QString PDFAppAnnotationInfo::formatDateTime(const QDateTime& dateTime)
{
    if (!dateTime.isValid())
    {
        return QString();
    }

    const QDateTime local = dateTime.toLocalTime();
    const QDate today = QDate::currentDate();
    const QString clock = PDFAppLocale::formatClock(local.time());
    if (local.date() == today)
    {
        return tr("Today %1").arg(clock);
    }
    if (local.date() == today.addDays(-1))
    {
        return tr("Yesterday %1").arg(clock);
    }
    return tr("%1 %2").arg(PDFAppLocale::formatDate(local.date()), clock);
}

QString PDFAppAnnotationInfo::sanitizeName(const QString& name)
{
    return PDFAppAnnotationInfoHelper::sanitizeLine(name, MAXIMUM_NAME_LENGTH);
}

QString PDFAppAnnotationInfo::getAuthorText(const QString& author)
{
    const QString text = PDFAppAnnotationInfoHelper::sanitizeLine(author, MAXIMUM_NAME_LENGTH);
    return text.isEmpty() ? tr("Anonymous") : text;
}

}   // namespace pdf
