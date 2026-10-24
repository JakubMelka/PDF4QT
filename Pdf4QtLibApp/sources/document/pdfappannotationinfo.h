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

#ifndef PDFAPPANNOTATIONINFO_H
#define PDFAPPANNOTATIONINFO_H

#include "pdfappglobal.h"

#include "pdfaccenthue.h"
#include "pdfannotation.h"
#include "pdfdocument.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QRectF>
#include <QString>

#include <vector>

namespace pdf
{

/// What package B shows of an annotation (the comment popup, the annotation bar,
/// the menus, the dialogs): the type, the page, the author, the texts, the dates,
/// the flags. Read from the untrusted dictionary, the texts are bounded.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationInfo
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppAnnotationInfo)

public:
    /// Longest author and subject shown, longest contents shown
    static constexpr qsizetype MAXIMUM_NAME_LENGTH = 256;
    static constexpr qsizetype MAXIMUM_CONTENTS_LENGTH = 64 * 1024;

    /// Most replies of a thread shown
    static constexpr size_t MAXIMUM_REPLIES = 1000;

    PDFObjectReference reference;
    PDFObjectReference pageReference;
    PDFInteger pageIndex = -1;
    AnnotationType type = AnnotationType::Invalid;
    QString author;
    QString subject;
    QString contents;
    QDateTime created;
    QDateTime modified;
    QRectF rectangle;                   ///< Rectangle on the page (page coordinates)
    PDFAnnotation::Flags flags;
    QByteArray iconName;                ///< Name of a sticky note / stamp
    bool isMarkup = false;
    bool isReply = false;               ///< A reply to another annotation (IRT)

    bool isValid() const { return type != AnnotationType::Invalid && reference.isValid(); }
    bool isLocked() const { return flags.testFlag(PDFAnnotation::Locked) || flags.testFlag(PDFAnnotation::ReadOnly); }
    bool isContentsLocked() const { return flags.testFlag(PDFAnnotation::LockedContents) || flags.testFlag(PDFAnnotation::ReadOnly); }
    bool isHidden() const { return flags.testFlag(PDFAnnotation::Hidden); }

    /// Date shown of the annotation (modified, else created)
    QDateTime getDate() const { return modified.isValid() ? modified : created; }

    /// Reads the annotation (an invalid info, when it is not an annotation on a page)
    static PDFAppAnnotationInfo read(const PDFDocument* document, PDFObjectReference annotation);

    /// The comment thread of the annotation: its replies (also the replies to the
    /// replies, each after the annotation it replies to), at most MAXIMUM_REPLIES
    static std::vector<PDFAppAnnotationInfo> readReplies(const PDFDocument* document, PDFObjectReference annotation);

    /// Name of the type ("Rectangle", "Sticky note", ...)
    static QString getTypeName(AnnotationType type);

    /// Title of the properties dialog of the type ("Properties of the rectangle")
    static QString getPropertiesTitle(AnnotationType type);

    /// Name of the annotation in a list (the type, a stamp with its name:
    /// Stamp "Approved")
    static QString getDisplayName(const PDFAppAnnotationInfo& info);

    /// Icon and hue of the badge of the type (as the rows of Notes)
    static void getBadge(AnnotationType type, bool isReply, QString* iconName, PDFAccentHue* hue);

    /// Date and time of a comment: "Today 14:05", "Yesterday 9:12", "Oct 19, 2026 14:05"
    /// (O17: the language of the user interface); empty for an invalid date
    static QString formatDateTime(const QDateTime& dateTime);

    /// Name of the author shown: the author, or "Anonymous" for an empty one
    static QString getAuthorText(const QString& author);

    /// A name of the document (an author, a subject) shown in a single line: at most
    /// MAXIMUM_NAME_LENGTH, control characters replaced by spaces, bidirectional
    /// controls removed (they would reorder the text around the name); thread safe
    static QString sanitizeName(const QString& name);
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONINFO_H
