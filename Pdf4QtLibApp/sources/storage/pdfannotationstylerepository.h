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

#ifndef PDFANNOTATIONSTYLEREPOSITORY_H
#define PDFANNOTATIONSTYLEREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <optional>

namespace pdf
{

/// Styles of the annotation tools (table annotation_style, step 4 plan chapter 4,
/// newapp_storage.md 4): one row per style id with a JSON object as its value.
/// Style rows keep the ids of PDFAnnotationStyleSettings ("Highlight", "Underline",
/// "Squiggly", "StrikeOut", "Shape", "Freehand", "Redact") with the value
/// {strokeColor, fillColor|null, penWidth}; tool rows ("tool.stickyNote", ...)
/// keep the last choice of a tool. The rows are untrusted input (another version,
/// another instance, a damaged file): a value, which is not a JSON object of a
/// sane size, is read as no row; unknown keys are ignored by the readers.
///
/// A value object: every read goes to the database (no cache), writes are short
/// transactions notified with the topic getTopic() (key = the style id). Usable in
/// any thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAnnotationStyleRepository
{
public:
    /// Topic of the change notifications
    static QString getTopic() { return QStringLiteral("annotation_style"); }

    /// Maximal length of a style id
    static constexpr qsizetype MAXIMUM_STYLE_ID_LENGTH = 128;

    /// Maximal size of a stored value (UTF-8 bytes of the JSON text)
    static constexpr qsizetype MAXIMUM_VALUE_SIZE = 16 * 1024;

    /// Maximal number of the rows read by getStyleIds
    static constexpr int MAXIMUM_ROW_COUNT = 1000;

    // Ids of the tool rows (step 4 plan chapter 4)
    static QString toolStickyNote() { return QStringLiteral("tool.stickyNote"); }     ///< {icon}
    static QString toolStamp() { return QStringLiteral("tool.stamp"); }               ///< {stamp}
    static QString toolFreeText() { return QStringLiteral("tool.freeText"); }         ///< {font, size, textColor, alignment, autoResize}
    static QString toolPageLink() { return QStringLiteral("tool.pageLink"); }         ///< {destinationType}
    static QString toolLastMarkup() { return QStringLiteral("tool.lastMarkup"); }     ///< {type}

    explicit PDFAnnotationStyleRepository(PDFDatabase* database);

    /// Value of the style id: nullopt, when there is no row, the database cannot
    /// be read or the stored value is not a JSON object of a sane size
    std::optional<QJsonObject> getValue(const QString& styleId, PDFDatabaseError* error = nullptr) const;

    /// Stores the value of the style id (a row, which has the same value, is not
    /// written, so no notification is sent)
    PDFDatabaseError setValue(const QString& styleId, const QJsonObject& value);

    /// Removes the row of the style id (the default style applies)
    PDFDatabaseError removeValue(const QString& styleId);

    /// Removes all rows (Restore defaults)
    PDFDatabaseError resetAll();

    /// Valid style ids of the rows (sorted)
    QStringList getStyleIds(PDFDatabaseError* error = nullptr) const;

    /// The style id is not empty, not longer than MAXIMUM_STYLE_ID_LENGTH and
    /// consists of printable ASCII characters
    static bool isValidStyleId(const QString& styleId);

private:
    QPointer<PDFDatabase> m_database;
};

}   // namespace pdf

#endif // PDFANNOTATIONSTYLEREPOSITORY_H
